#!/bin/bash
# Regression test: drive the vcam fake-libusb shim (usb-vcam) with a libgphoto2
# client against the Fuji models (USB card-reader transport, --usb).
#
# Verifies device detection, session init, config tree, file listing,
# live view (InitiateOpenCapture + object 0x80000001), capture+download
# (InitiateCapture + ObjectAdded / GetObjectHandles-diff path), the
# 0xD208 action control (cameraaction AF round trip, bulb flow) and that
# the gphoto2 fuji capture flow leaves no orphan objects on the camera.
#
# Requirements:
#   - libgphoto2 built into $GP_INSTALL (see scripts/gp_client.md)
#   - scripts/gp_client built against it ($GP_CLIENT)
#
# Usage: scripts/regression-fuji-gp.sh [model ...]   (default: all models)
# Copyright Daniel C - GNU Lesser General Public License v2.1

set -u

MODELS=${*:-"fuji_x_a2 fuji_x_t20 fuji_x_t2 fuji_x_s10 fuji_x_t4 fuji_gfx100ii fuji_x_h1 fuji_x_dev fuji_x_f10 fuji_x30 fuji_x_t30"}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
GP_INSTALL=${GP_INSTALL:-/tmp/opencode/gp-install}
GP_CLIENT=${GP_CLIENT:-/tmp/opencode/gp_client}
SHIM=${SHIM:-$REPO_ROOT/build/libusb-vcam.so}

fail() {
	echo "FAIL: $1"
	exit 1
}

[ -x "$GP_CLIENT" ] || fail "gp_client not built: $GP_CLIENT (see scripts/gp_client.md)"
[ -f "$SHIM" ] || fail "shim not built: $SHIM (run: cmake -B build && cmake --build build)"

# Exercise captures on an empty card regardless of local, untracked fixtures.
mkdir -p /tmp/opencode || fail "cannot create output directory"
CARD_DIR=$(mktemp -d /tmp/opencode/fuji-card.XXXXXX) || fail "cannot create empty card"
trap 'rmdir "$CARD_DIR"' EXIT

run() {
	env VCAM_USB_ARGS="--usb --fs $CARD_DIR" \
		LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 90 "$GP_CLIENT" "$@"
}

for MODEL in $MODELS; do
	export VCAM_USB_MODEL=$MODEL
	echo "== model: $MODEL =="

	out=$(run summary 2>/dev/null) || fail "summary command exited nonzero"
	echo "$out" | grep -q "Manufacturer: FUJIFILM" || fail "summary did not report a Fuji model"
	echo "summary OK"

	out=$(run config 2>/dev/null) || fail "config command exited nonzero"
	echo "config OK"

	run list >/dev/null 2>&1 || fail "list command exited nonzero"
	echo "list OK"

	rm -f /tmp/opencode/preview.jpg
	run preview >/dev/null 2>&1 || fail "preview command exited nonzero"
	[ -s /tmp/opencode/preview.jpg ] || fail "preview.jpg missing or empty"
	file /tmp/opencode/preview.jpg | grep -q "JPEG image data" || fail "preview.jpg is not a JPEG"
	echo "preview OK"

	# Capture+download: InitiateCapture creates the object in the card tree,
	# gphoto2 finds it via the ObjectAdded event or the GetObjectHandles diff.
	# The capture flow (focus + shoot via 0xD208) must produce exactly one
	# object and the client's delete must leave none behind.
	rm -f /tmp/opencode/capt00*.jpg /tmp/opencode/GPH_*.JPG
	out=$(run download 2>/dev/null) || fail "capture+download command exited nonzero"
	echo "$out" | grep -q "orphans after delete: 0" || \
		fail "capture left an orphan object on the camera ($(echo "$out" | grep orphans))"
	shopt -s nullglob
	capfiles=(/tmp/opencode/capt00*.jpg /tmp/opencode/GPH_*.JPG)
	shopt -u nullglob
	[ ${#capfiles[@]} -ge 1 ] || fail "capture produced no image"
	file "${capfiles[0]}" | grep -q "JPEG image data" || fail "captured image is not a JPEG"
	echo "capture+download OK"

	# Two captures in one session: both must download, no orphans. (vcam
	# reuses the GPH_0099.JPG name per capture, so client-side files are 1)
	rm -f /tmp/opencode/capt00*.jpg /tmp/opencode/GPH_*.JPG
	out=$(run download2 2>/dev/null) || fail "download2 command exited nonzero"
	[ "$(echo "$out" | grep -c '^downloaded:')" -eq 2 ] || fail "download2 downloaded $(echo "$out" | grep -c '^downloaded:') images, expected 2"
	[ "$(echo "$out" | grep -c 'orphans after delete: 0')" -eq 2 ] || \
		fail "download2 orphan check ($(echo "$out" | grep orphans | tr '\n' ' '))"
	file /tmp/opencode/GPH_*.JPG 2>/dev/null | grep -q "JPEG image data" || fail "download2 image is not a JPEG"
	echo "download2 OK"

	# 0xD208 camera action control through the gphoto2 config layer
	run actionaf >/dev/null 2>&1 || fail "actionaf command exited nonzero"
	echo "cameraaction AF round trip OK"

	# AF drive via the autofocusdrive widget: runs gphoto2's AF flow
	# (0xD208 focus + InitiateCapture + 0xD209 poll + stop) and must
	# create no image
	run afdrive >/dev/null 2>&1 || fail "afdrive command exited nonzero"
	echo "autofocusdrive OK"

	# Bulb via the config layer: open creates nothing, close exactly one image
	run bulb >/dev/null 2>&1 || fail "bulb command exited nonzero"
	echo "bulb OK"
done

echo "ALL OK"
