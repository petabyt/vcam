#!/bin/bash
# Regression test: drive the vcam fake-libusb shim (usb-vcam) with a libgphoto2 client.
#
# Verifies the gphoto2 <-> libusb shim integration (src/usb/libusb.c) for every
# Canon model: device detection, session init, config tree, file listing,
# live view and remote capture with download (SDRAM and/or card path).
#
# Requirements:
#   - libgphoto2 built into $GP_INSTALL (see scripts/gp_client.md)
#   - scripts/gp_client built against it ($GP_CLIENT)
#
# Usage: scripts/regression-canon-gp.sh [model ...]   (default: all models)
# Copyright Daniel C - GNU Lesser General Public License v2.1

set -u

MODELS=${*:-"canon_1300d canon_5d3 canon_90d canon_200d canon_r canon_g7x3 eos_m"}
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
	env LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 90 "$GP_CLIENT" "$@"
}

for MODEL in $MODELS; do
	export VCAM_USB_MODEL=$MODEL
	echo "== model: $MODEL =="

	out=$(run summary) || fail "summary command exited nonzero"
	echo "$out" | grep -q "Model: Canon" || fail "summary did not report a Canon model"
	echo "summary OK"

	out=$(run config) || fail "config command exited nonzero"
	echo "$out" | grep -q "Capture Target" || fail "config tree missing Capture Target"
	echo "config OK"

	run list >/dev/null || fail "list command exited nonzero"
	echo "list OK"

	rm -f /tmp/opencode/preview.jpg
	run preview >/dev/null || fail "preview command exited nonzero"
	[ -s /tmp/opencode/preview.jpg ] || fail "preview.jpg missing or empty"
	echo "preview OK"

	# eos_m is a PID-0 stub: it enumerates as a generic PTP camera and has
	# no working remote-capture event stream - capture is not exercised
	case "$MODEL" in
	eos_m)
		echo "capture+download skipped (stub model)"
		continue
		;;
	esac

	rm -f /tmp/opencode/capt0*.jpg /tmp/opencode/GPH_0*.JPG /tmp/opencode/GPH_0*.jpg
	run download2 >/dev/null || fail "capture+download command exited nonzero"
	if [ ! -s /tmp/opencode/capt0000.jpg ] && [ ! -s /tmp/opencode/GPH_0099.JPG ]; then
		fail "capture produced no image"
	fi
	echo "capture+download OK"
done

echo "ALL OK"