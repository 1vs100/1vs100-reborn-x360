#!/bin/sh
# Build, and deploy the loader when XEEDEE_HOST names a reachable console.
#   XEDK=~/xdk XEEDEE_HOST=<console ip> ./build.sh
set -e
export XEDK="${XEDK:-$HOME/xdk}"
cmake --preset xdk
cmake --build --preset xdk

if [ -z "$XEEDEE_HOST" ]; then
    echo "[-] XEEDEE_HOST not set, not deploying loader xex"
elif ping -c 1 "$XEEDEE_HOST" >/dev/null 2>&1; then
    echo "[+] Deploying loader..."
    xeedee-cli file put build/1vs100Reborn.xex 'Hdd:\1vs100Reborn.xex'
else
    echo "[-] Console $XEEDEE_HOST offline, not deploying loader xex"
fi
