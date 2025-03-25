#!/bin/sh
# Script to compile and install
set -e

cd ~
rm -rf /tmp/vcam
git clone https://github.com/petabyt/vcam --depth 1 --recurse-submodules /tmp/vcam
cd /tmp/vcam
make install
