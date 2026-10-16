#!/bin/bash
# Run every vendor regression test suite against the fake-libusb shim. Exits nonzero
# when any suite fails; the full output is suitable for CI log artifacts.
#
# Environment (see scripts/gp_client.md):
#   GP_INSTALL   libgphoto2 install prefix (e.g. /usr for system packages)
#   GP_CLIENT    path to the compiled scripts/gp_client
#   SHIM         path to build/libusb-vcam.so
#   GPHOTO2_CLI  optional gphoto2 CLI for the nikon/sony CLI flows
#
# Usage: scripts/run-all-regression-tests.sh
# Copyright Daniel C - GNU Lesser General Public License v2.1

set -u

DIR=$(cd "$(dirname "$0")" && pwd)
failed=""

# The regression test scripts save preview.jpg / captured images under /tmp/opencode
mkdir -p /tmp/opencode

for script in regression-canon-gp.sh regression-nikon-gp.sh regression-sony-gp.sh regression-fuji-gp.sh; do
	echo ""
	echo "================ $script ================"
	if bash "$DIR/$script"; then
		echo ">>> $script PASSED"
	else
		echo ">>> $script FAILED"
		failed="$failed $script"
	fi
done

echo ""
if [ -n "$failed" ]; then
	echo "REGRESSION TEST RESULT: FAILED:$failed"
	exit 1
fi
echo "REGRESSION TEST RESULT: ALL PASSED"
exit 0
