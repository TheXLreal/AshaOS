#!/usr/bin/env bash
# Build the Android 16 AshaOS/LineageOS ARM64 system image for testing.
set -Eeo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
output_dir=${ASHAOS_GSI_OUTPUT_DIR:-/mnt/c/AshaOS/builds/GSI}
log_file="$output_dir/build.log"
status_file="$output_dir/status.txt"
artifact="$output_dir/AshaOS-GSI-Android16-arm64.img"

mkdir -p "$output_dir"
: > "$log_file"
exec >> "$log_file" 2>&1
printf 'RUNNING %s\n' "$(date --iso-8601=seconds)" > "$status_file"
printf '%s\n' "$$" > "$output_dir/pid.txt"

finish() {
    result=$?
    trap - EXIT
    if [ "$result" -eq 0 ]; then
        printf 'SUCCESS %s\n' "$(date --iso-8601=seconds)" > "$status_file"
    else
        printf 'FAILED exit=%s %s\n' "$result" "$(date --iso-8601=seconds)" > "$status_file"
    fi
    printf '\nAshaOS GSI build finished: exit=%s at %s\n' \
        "$result" "$(date --iso-8601=seconds)"
}
trap finish EXIT

printf 'AshaOS GSI build started: %s\n' "$(date --iso-8601=seconds)"
printf 'Source: %s\nOutput: %s\n' "$repo_root" "$artifact"
free -h
df -h "$repo_root" "$output_dir"

cd "$repo_root"
source build/envsetup.sh
lunch lineage_gsi_ashaos_arm64-bp4a-userdebug

# One Ninja job limits peak memory use on this 13 GiB RAM workstation.
m systemimage -j1

image=out/target/product/generic_arm64/system.img
app=out/target/product/generic_arm64/system/product/app/AudioAsha/AudioAsha.apk
bluetooth_apex=out/target/product/generic_arm64/system/apex/com.android.bt.capex
framework=out/target/product/generic_arm64/system/framework/framework.jar
test -s "$image"
test -s "$app"
test -s "$bluetooth_apex"
test -s "$framework"

cp "$image" "$artifact.partial"
mv "$artifact.partial" "$artifact"
sha256sum "$artifact" > "$artifact.sha256"
gzip -1 -c "$artifact" > "$artifact.gz.partial"
mv "$artifact.gz.partial" "$artifact.gz"
sha256sum "$artifact.gz" > "$artifact.gz.sha256"
ls -lh "$artifact" "$artifact.gz" "$app" "$bluetooth_apex"
