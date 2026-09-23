#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)

if ! command -v cmake >/dev/null || ! pkg-config --exists Qt6Widgets; then
  echo "Install build dependencies first:"
  echo "  sudo apt install build-essential cmake qt6-base-dev pulseaudio-utils android-tools-adb"
  exit 1
fi

"$root/build-linux.sh"
sudo cmake --install "$root/build"
echo "AshaOS is installed. Open it from your application menu."
