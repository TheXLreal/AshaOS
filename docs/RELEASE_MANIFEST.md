# AshaOS Beta 1.7 build record

Build date: 2026-09-23. Android base: LineageOS 23.2 / Android 16.

| Component | Local source revision used for Android build |
| --- | --- |
| AshaOS device and Audio-Asha app | `6168be7` (the direct-network app was introduced in `b198e86`) |
| Bluetooth Direct source | `673278ea9e` (desktop Linux integration followed in `220d6d4e61`) |
| Android framework compatibility fix | `a7df63da2d25` |

Desktop clients were subsequently rebuilt as version 1.7. Their later source
commits affect desktop packaging and help text, not the Android system image.
The source snapshots under `device/` and `clients/` plus the two patches in
this repository are the publishing layout; Google/Qualcomm vendor binaries
are deliberately excluded.

| Release asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `lineage-23.2-20260923-UNOFFICIAL-ashaos_redfin-direct-network.zip` | 1266737342 | `1b65414ce5177bb2b47a11cb325cd770c3c3124ad673a2080a6a235062e0f9c8` |
| `AshaOS-GSI-Android16-arm64.img.gz` | 1210713852 | `f1db75103fdba46f39c70121591f9d3ba185e101ed2e4255b433e08b63a3430c` |
| `ashaos-linux_1.7.0_amd64.deb` | see `SHA256SUMS.txt` | `14f709e4414121dd7bff8a82cb019f0872a2a922ec8a27b493633a81aa834e39` |
| `windows-ashaos-1.7.exe` | see `SHA256SUMS.txt` | `3bffb9e4b78d4c17b607d723a91dd7b6bad3f299a6486747bc9db7277981b26c` |

The OTA ZIP passed CRC and metadata checks; GSI gzip integrity and its
uncompressed filesystem contents were checked. Installation, boot and audio
operation of these exact artifacts on end-user devices still require testing.
