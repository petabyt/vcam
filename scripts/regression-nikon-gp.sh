#!/bin/bash
# Regression test: drive the vcam fake-libusb shim (usb-vcam) with a libgphoto2
# client and the gphoto2 CLI against the Nikon models.
#
# Verifies device detection, session init (incl. GetVendorPropCodes 0x90CA /
# 0x9439), the config tree, file listing, live view (StartLiveView/GetLiveViewImg)
# and remote capture on both the SDRAM path (0x9207 + ObjectAddedInSDRAM /
# CaptureCompleteRecInSdram events) and the card path (ObjectAdded /
# CaptureComplete + card download + delete).
#
# Requirements:
#   - libgphoto2 built into $GP_INSTALL (see scripts/gp_client.md)
#   - scripts/gp_client built against it ($GP_CLIENT)
#   - gphoto2 CLI ($GPHOTO2_CLI, optional but strongly recommended)
#
# Usage: scripts/regression-nikon-gp.sh [model ...]   (default: all models)
# Copyright Daniel C - GNU Lesser General Public License v2.1

set -u

MODELS=${*:-"nikon_d7500 nikon_z6_2"}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
GP_INSTALL=${GP_INSTALL:-/tmp/opencode/gp-install}
GP_CLIENT=${GP_CLIENT:-/tmp/opencode/gp_client}
GPHOTO2_CLI=${GPHOTO2_CLI:-/tmp/opencode/gphoto2-2.5.28/gphoto2/gphoto2}
SHIM=${SHIM:-$REPO_ROOT/build/libusb-vcam.so}
TEST_HOME=${TEST_HOME:-$(mktemp -d /tmp/vcam-nikon-regtest.XXXXXX)}

fail() {
	echo "FAIL: $1"
	exit 1
}

[ -x "$GP_CLIENT" ] || fail "gp_client not built: $GP_CLIENT (see scripts/gp_client.md)"
[ -f "$SHIM" ] || fail "shim not built: $SHIM (run: cmake -B build && cmake --build build)"

# gphoto2 persists capturetarget in $HOME/.config/gphoto/settings and its
# mkdir() is not recursive - pre-create the directory and start clean so the
# defaults (capturetarget=sdram) are in effect
mkdir -p "$TEST_HOME/.config/gphoto"
rm -f "$TEST_HOME/.config/gphoto/settings"

# gphoto2 logs and the vcam core share stdout; drop the noise
quiet() { grep -vE "^\[VCAM\]|^libusb|^Request phase|^Time since|ptp_getdeviceprop|Reading bulk|Passing h|getdevicepropdesc"; }

run_gp_client() {
	env HOME="$TEST_HOME" LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 120 "$GP_CLIENT" "$@"
}

run_gp_cli() {
	env HOME="$TEST_HOME" LD_PRELOAD="$SHIM" \
		LD_LIBRARY_PATH="$GP_INSTALL/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		timeout 120 "$GPHOTO2_CLI" "$@"
}

for MODEL in $MODELS; do
	export VCAM_USB_MODEL=$MODEL
	echo "== model: $MODEL =="

	# DeviceInfo-derived expectations
	case "$MODEL" in
	nikon_d7500) MODEL_STR="Model: D7500"; CLI_STR="Nikon DSC D7500" ;;
	nikon_z6_2)  MODEL_STR="Model: Z 6_2"; CLI_STR="Nikon Z6_2" ;;
	*) fail "unknown model $MODEL" ;;
	esac

	out=$(run_gp_client summary 2>/dev/null | quiet) || fail "summary command exited nonzero"
	echo "$out" | grep -q "$MODEL_STR" || fail "summary did not report '$MODEL_STR'"
	echo "$out" | grep -q "Vendor Extension ID: 0xa" || fail "summary missing Vendor Extension ID 0xa"
	echo "summary OK"

	out=$(run_gp_client config 2>/dev/null | quiet) || fail "config command exited nonzero"
	echo "$out" | grep -q "Capture Target" || fail "config tree missing Capture Target"
	echo "config OK"

	run_gp_client list >/dev/null 2>&1 || fail "list command exited nonzero"
	echo "list OK"

	rm -f /tmp/opencode/preview.jpg
	run_gp_client preview >/dev/null 2>&1 || fail "preview command exited nonzero"
	[ -s /tmp/opencode/preview.jpg ] || fail "preview.jpg missing or empty"
	file /tmp/opencode/preview.jpg | grep -q "JPEG image data" || fail "preview.jpg is not a JPEG"
	echo "preview OK"

	# Two captures in one session through the SDRAM path (default target)
	rm -f /tmp/opencode/capt00*.jpg
	run_gp_client download2 >/dev/null 2>&1 || fail "capture+download command exited nonzero"
	[ -s /tmp/opencode/capt0000.jpg ] && [ -s /tmp/opencode/capt0001.jpg ] || fail "capture produced no images"
	echo "capture+download (gp_client, sdram) OK"

	if [ -x "$GPHOTO2_CLI" ]; then
		out=$(run_gp_cli --auto-detect 2>/dev/null | quiet) || fail "gphoto2 --auto-detect failed"
		echo "$out" | grep -q "$CLI_STR" || fail "gphoto2 did not detect '$CLI_STR'"
		echo "gphoto2 auto-detect OK"

		rm -f /tmp/opencode/regression_nikon_*.jpg
		run_gp_cli --capture-image-and-download --force-overwrite \
			--filename /tmp/opencode/regression_nikon_sdram.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --capture-image-and-download (sdram) failed"
		[ -s /tmp/opencode/regression_nikon_sdram.jpg ] || fail "sdram capture produced no image"
		echo "gphoto2 capture+download (sdram) OK"

		run_gp_cli --set-config capturetarget="Memory card" >/dev/null 2>&1 \
			|| fail "gphoto2 --set-config capturetarget failed"
		run_gp_cli --capture-image-and-download --force-overwrite \
			--filename /tmp/opencode/regression_nikon_card.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --capture-image-and-download (card) failed"
		[ -s /tmp/opencode/regression_nikon_card.jpg ] || fail "card capture produced no image"
		echo "gphoto2 capture+download (card) OK"

		run_gp_cli --set-config capturetarget="Internal RAM" >/dev/null 2>&1 \
			|| fail "gphoto2 --set-config capturetarget (back to sdram) failed"

		run_gp_cli --trigger-capture --wait-event-and-download=FILE --force-overwrite \
			--filename /tmp/opencode/regression_nikon_trig.jpg >/dev/null 2>&1 \
			|| fail "gphoto2 --trigger-capture --wait-event-and-download failed"
		[ -s /tmp/opencode/regression_nikon_trig.jpg ] || fail "trigger capture produced no image"
		echo "gphoto2 trigger-capture + wait-event OK"
	else
		echo "gphoto2 CLI not found ($GPHOTO2_CLI) - skipping CLI tests"
	fi
done

rm -rf "$TEST_HOME"
echo "ALL OK"
