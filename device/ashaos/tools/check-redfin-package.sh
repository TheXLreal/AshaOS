#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <redfin-ota-or-fastboot.zip>" >&2
    exit 2
fi

package=$1

for tool in unzip sha256sum grep; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required tool: $tool" >&2
        exit 2
    fi
done

if [ ! -f "$package" ]; then
    echo "Package not found: $package" >&2
    exit 2
fi

entries=$(unzip -Z1 "$package")
metadata=""

if printf '%s\n' "$entries" | grep -qx 'android-info.txt'; then
    metadata=$(unzip -p "$package" android-info.txt)
elif printf '%s\n' "$entries" | grep -qx 'META-INF/com/android/metadata'; then
    metadata=$(unzip -p "$package" META-INF/com/android/metadata)
elif printf '%s\n' "$entries" | grep -qx 'OTA/android-info.txt'; then
    metadata=$(unzip -p "$package" OTA/android-info.txt)
else
    echo "Refusing package without Android device metadata." >&2
    exit 1
fi

if ! printf '%s\n' "$metadata" | grep -Eq '(^|[=,[:space:]])redfin($|[|,[:space:]])'; then
    echo "Refusing package: metadata does not identify Pixel 5 (redfin)." >&2
    exit 1
fi

dangerous='(^|/)(abl|aop|devcfg|featenabler|hyp|keymaster|modem|qupfw|tz|uefisecapp|xbl|xbl_config|bootloader[^/]*|radio[^/]*)[.]img$'
found=$(printf '%s\n' "$entries" | grep -E "$dangerous" || true)
if [ -n "$found" ]; then
    echo "Refusing package containing low-level firmware images:" >&2
    printf '%s\n' "$found" >&2
    exit 1
fi

echo "Device metadata: redfin"
sha256sum "$package"
echo "Package passed AshaOS non-firmware safety checks."
