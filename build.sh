#!/bin/sh
# Usage: XEDK=~/xdk ./build.sh
set -e
export XEDK="${XEDK:-$HOME/xdk}"
python3 ./cmake/gen-patch-docs.py
cmake --preset xdk
cmake --build --preset xdk
