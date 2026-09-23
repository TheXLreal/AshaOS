#!/usr/bin/env bash
# Apply AshaOS source additions to a clean LineageOS 23.2 checkout.
set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 /path/to/lineage-23.2" >&2
    exit 2
fi

ashaos_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
android_root=$(realpath "$1")
bluetooth="$android_root/packages/modules/Bluetooth"
framework="$android_root/frameworks/base"

[[ -d "$bluetooth/.git" && -d "$framework/.git" ]] || {
    echo "LineageOS source checkout not found at $android_root" >&2
    exit 1
}
[[ ! -e "$android_root/device/ashaos" ]] || {
    echo "device/ashaos already exists; inspect it before copying." >&2
    exit 1
}

git -C "$bluetooth" apply --check "$ashaos_root/patches/bluetooth-direct-asha.patch"
git -C "$framework" apply --check "$ashaos_root/patches/framework-pixel-compat.patch"

mkdir -p "$android_root/device"
cp -a "$ashaos_root/device/ashaos" "$android_root/device/ashaos"
git -C "$bluetooth" apply "$ashaos_root/patches/bluetooth-direct-asha.patch"
git -C "$framework" apply "$ashaos_root/patches/framework-pixel-compat.patch"

echo "AshaOS source added. Review git diff in Bluetooth and frameworks/base."
