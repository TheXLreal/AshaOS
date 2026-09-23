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
| `windows-ashaos-1.7.exe` | see `SHA256SUMS.txt` | `2577c0aba1906782724b6739dda739d1539016800f430993bd4d5e3e9801e284` |
| `boot.img` | 100663296 | `b71de8380be16c28216816f2771b9c38f0cbc0203fcc08f862ef3eb303ac951c` |
| `dtbo.img` | 16777216 | `7241ce69a548bf1a2747d0140c5b3f3edd944c9e19971ab679392b732cb9ea6f` |
| `vendor_boot.img` | 100663296 | `71acbef2902f16ec911a4f0b58707ed283f93d9f0b683274bceb5816b4637958` |

The three standalone partition images match the image sizes and SHA-256 hashes
recorded in the published OTA's `payload.bin` manifest. The top-level
`out/target/product/redfin/*.img` files have different hashes; use the copies
under `obj/PACKAGING/target_files_intermediates/.../IMAGES/`.

The Windows executable was rebuilt from source commit `22c6b73` after the
initial Beta 1.7 publication to include direct sender timing and silence
recovery fixes. The Android OTA and GSI were not rebuilt for this desktop fix.

The OTA ZIP passed CRC and metadata checks; GSI gzip integrity and its
uncompressed filesystem contents were checked. Installation, boot and audio
operation of these exact artifacts on end-user devices still require testing.
