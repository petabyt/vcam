#!/bin/bash
# Regression test: drive the vcam fake-libusb shim (usb-vcam) with a libgphoto2
# client and the gphoto2 CLI against the Sony models.
#
# Verifies the SDIO mode-3 handshake (0x9201/0x9202/0x9205/0x9209), the
# property cache, live view (GetObject 0xffffc002) and remote capture
# (0x9207 shutter controls -> 0xD213 FocusFound -> 0xD215 ObjectInMemory ->
# GetObject 0xffffc001) on both the gp_client and gphoto2 CLI paths.
#
# Requirements:
#   - libgphoto2 built into $GP_INSTALL (see scripts/gp_client.md)
#   - scripts/gp_client built against it ($GP_CLIENT)
#   - gphoto2 CLI ($GPHOTO2_CLI, optional but strongly recommended)
#
# Usage: scripts/regression-sony-gp.sh [model ...]   (default: all models)
# Copyright Daniel C - GNU Lesser General Public License v2.1

set -u

MODELS=${*:-"sony_a7m4 sony_a7r5"}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
GP_INSTALL=${GP_INSTALL:-/tmp/opencode/gp-install}
GP_CLIENT=${GP_CLIENT:-/tmp/opencode/gp_client}
GPHOTO2_CLI=${GPHOTO2_CLI:-/tmp/opencode/gphoto2-2.5.28/gphoto2/gphoto2}
SHIM=${SHIM:-$REPO_ROOT/build/libusb-vcam.so}
TEST_HOME=${TEST_HOME:-$(mktemp -d /tmp/vcam-sony-regtest.XXXXXX)}

fail() {
	echo "FAIL: $1"
	exit 1
}

[ -x "$GP_CLIENT" ] || fail "gp_client not built: $GP_CLIENT (see scripts/gp_client.md)"
[ -f "$SHIM" ] || fail "shim not built: $SHIM (run: cmake -B build && cmake --build build)"

# gphoto2 persists settings in $HOME/.config/gphoto/settings and its
# mkdir() is not recursive - pre-create the directory and start clean
mkdir -p "$TEST_HOME/.config/gphoto"
rm -f "$TEST_HOME/.config/gphoto/settings"

# gphoto2 logs and the vcam core share stdout; drop the noise
quiet() { grep -vE "^\[VCAM\]|^libusb|^Request phase|^Time since|ptp_getdeviceprop|Reading bulk|Passing h|getdevicepropdesc"; }

run_gp_client() {
	env HOME="$TEST_HOME" LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 180 "$GP_CLIENT" "$@"
}

run_gp_cli() {
	env HOME="$TEST_HOME" LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 180 "$GPHOTO2_CLI" "$@"
}

for MODEL in $MODELS; do
	export VCAM_USB_MODEL=$MODEL
	echo "== model: $MODEL =="

	# DeviceInfo-derived expectations
	case "$MODEL" in
	sony_a7m4) MODEL_STR="Model: ILCE-7M4";  CLI_STR="Sony Alpha-A7 IV (PC Control)" ;;
	sony_a7r5) MODEL_STR="Model: ILCE-7RM5"; CLI_STR="Sony ILCE-7RM5 (PC Control)" ;;
	*) fail "unknown model $MODEL" ;;
	esac

	out=$(run_gp_client summary 2>/dev/null | quiet) || fail "summary command exited nonzero"
	echo "$out" | grep -q "$MODEL_STR" || fail "summary did not report '$MODEL_STR'"
	echo "$out" | grep -q "Vendor Extension ID: 0x11" || fail "summary missing Vendor Extension ID 0x11"
	echo "$out" | grep -q "Sony Capture" || fail "summary missing Sony Capture capability"
	echo "summary OK"

	out=$(run_gp_client config 2>/dev/null | quiet) || fail "config command exited nonzero"
	echo "$out" | grep -q "Capture Target" || fail "config tree missing Capture Target"
	echo "$out" | grep -q "Shutter Speed" || fail "config tree missing Shutter Speed"
	echo "config OK"

	run_gp_client list >/dev/null 2>&1 || fail "list command exited nonzero"
	echo "list OK"

	rm -f /tmp/opencode/preview.jpg
	run_gp_client preview >/dev/null 2>&1 || fail "preview command exited nonzero"
	[ -s /tmp/opencode/preview.jpg ] || fail "preview.jpg missing or empty"
	file /tmp/opencode/preview.jpg | grep -q "JPEG image data" || fail "preview.jpg is not a JPEG"
	echo "preview OK"

	# Two captures in one session through the SDRAM path (0xffffc001);
	# the second capture exercises the stale-image cleanup via 0xD215.
	# gphoto2 names these capt_DSC_XXXX.JPG (master, uses the object's
	# filename) or capt0000.jpg (<= 2.5.31, generic counter) - accept both
	rm -f /tmp/opencode/capt*.jpg /tmp/opencode/capt*.JPG
	run_gp_client download2 >/dev/null 2>&1 || fail "capture+download command exited nonzero"
	shopt -s nullglob
	capfiles=(/tmp/opencode/capt*.jpg /tmp/opencode/capt*.JPG)
	shopt -u nullglob
	[ ${#capfiles[@]} -ge 2 ] || fail "capture produced no images"
	file "${capfiles[0]}" | grep -q "JPEG image data" || fail "captured image is not a JPEG"
	echo "capture+download (gp_client, sdram) OK"

	if [ -x "$GPHOTO2_CLI" ]; then
		out=$(run_gp_cli --auto-detect 2>/dev/null | quiet) || fail "gphoto2 --auto-detect failed"
		echo "$out" | grep -q "$CLI_STR" || fail "gphoto2 did not detect '$CLI_STR'"
		echo "gphoto2 auto-detect OK"

		rm -f /tmp/opencode/regression_sony_*.jpg
		run_gp_cli --capture-image-and-download --force-overwrite \
			--filename /tmp/opencode/regression_sony_cap.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --capture-image-and-download failed"
		[ -s /tmp/opencode/regression_sony_cap.jpg ] || fail "capture produced no image"
		echo "gphoto2 capture+download OK"

		run_gp_cli --capture-preview --force-overwrite \
			--filename /tmp/opencode/regression_sony_lv.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --capture-preview failed"
		[ -s /tmp/opencode/thumb_regression_sony_lv.jpg ] || fail "live view produced no image"
		echo "gphoto2 capture-preview OK"

		run_gp_cli --trigger-capture --wait-event-and-download=FILE --force-overwrite \
			--filename /tmp/opencode/regression_sony_trig.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --trigger-capture --wait-event-and-download failed"
		[ -s /tmp/opencode/regression_sony_trig.jpg ] || fail "trigger capture produced no image"
		echo "gphoto2 trigger-capture + wait-event OK"

		# Property writes go through 0x9205; gphoto2 caches propdescs for
		# 2 seconds, hence the wait-event before reading back
		out=$(run_gp_cli --set-config shutterspeed=1/250 --wait-event=4s \
			--get-config shutterspeed 2>/dev/null | quiet) \
			|| fail "gphoto2 shutter speed set/get failed"
		echo "$out" | grep -q "Current: 1/250" || fail "shutter speed set did not stick"
		out=$(run_gp_cli --get-config capturetarget 2>/dev/null | quiet) \
			|| fail "gphoto2 --get-config capturetarget failed"
		echo "$out" | grep -q "Choice: 2 card" || fail "capturetarget choices missing"
		echo "gphoto2 config set/get OK"
	else
		echo "gphoto2 CLI not found ($GPHOTO2_CLI) - skipping CLI tests"
	fi
done

rm -rf "$TEST_HOME"
echo "ALL OK"
