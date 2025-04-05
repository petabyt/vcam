#!/bin/sh
# Script to compile and install vcam, for CI
set -e

cd ~
rm -rf /tmp/vcam
git clone https://github.com/petabyt/vcam --depth 1 --recurse-submodules /tmp/vcam
cd /tmp/vcam
cmake -B build
cmake --build build
cmake --install build
