#!/bin/bash
# Regression test: drive the vcam fake-libusb shim (usb-vcam) with a libgphoto2
# client against the Fuji models (USB card-reader transport, --usb).
#
# Verifies device detection, session init, config tree, file listing,
# live view (InitiateOpenCapture + object 0x80000001) and capture+download
# (InitiateCapture + ObjectAdded / GetObjectHandles-diff path).
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

run() {
	env VCAM_USB_ARGS="--usb" \
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
	# gphoto2 finds it via the ObjectAdded event or the GetObjectHandles diff
	rm -f /tmp/opencode/capt00*.jpg /tmp/opencode/GPH_*.JPG
	run download >/dev/null 2>&1 || fail "capture+download command exited nonzero"
	shopt -s nullglob
	capfiles=(/tmp/opencode/capt00*.jpg /tmp/opencode/GPH_*.JPG)
	shopt -u nullglob
	[ ${#capfiles[@]} -ge 1 ] || fail "capture produced no image"
	file "${capfiles[0]}" | grep -q "JPEG image data" || fail "captured image is not a JPEG"
	echo "capture+download OK"
done

echo "ALL OK"
