# AshaOS Android 16 ARM64 GSI

The `lineage_gsi_ashaos_arm64-bp4a-userdebug` product inherits the
LineageOS 23.2 ARM64 GSI and adds the AshaOS audio app and UI defaults. Its
primary artifact is `system.img`; it does not replace a device's vendor
drivers, boot image, or firmware. Compatibility and ASHA audio behavior still
need testing on each target device.

The generic product does not set Pixel 5's experimental BPF kernel override.
Device-specific overrides must stay in separate variants; otherwise they can
break boot on unrelated devices.

Run `tools/build-gsi-arm64.sh` from Ubuntu to build only the system image.
The script uses one Ninja job, checks that `AudioAsha.apk` was staged inside
`system/product`, and copies the resulting image to
`C:\AshaOS\builds\GSI\AshaOS-GSI-Android16-arm64.img`, plus a gzip-compressed
copy. It writes SHA-256 checksums alongside both images.

The build log and current state are in `C:\AshaOS\builds\GSI\build.log`
and `status.txt`. In PowerShell, follow the log with:

```powershell
Get-Content 'C:\AshaOS\builds\GSI\build.log' -Tail 40 -Wait
```

Press Ctrl+C to stop following the log; it does not stop the build. To check
whether the build is still running or has succeeded or failed:

```powershell
Get-Content 'C:\AshaOS\builds\GSI\status.txt'
```

This is a userdebug development image. Do not publish it as a tested universal
release until device compatibility, audio routing, and flashing instructions
have been validated.

## Pixel 5 DSU smoke test (2026-09-22)

The first ARM64 image installed through DSU on an unlocked Pixel 5 running the
device-specific AshaOS Android 16 build. `gsi_tool enable -s` succeeded, but
the phone returned to the device-specific system after reboot. The recorded
boot reason was `reboot,bpfloader-failed`; `gsi_tool status` then showed
`installed` and `disabled`. The GSI has **not** been shown to boot on Pixel 5.

Do not repeat Restart or flash the image permanently as a workaround. Capture
the previous-boot BPF loader / kernel verifier error before changing the image.
The ordinary post-reboot log did not contain that error, and `logcat -L` was
unavailable on this build. Compatibility with other devices remains untested.
## Pixel 5 diagnostic DSU attempt (2026-09-22)

A separate diagnostic image commented out `reboot_on_failure
reboot,bpfloader-failed` in the BPF loader init file for that build only. DSU
installation and enablement succeeded, but the phone did not reach Android or
expose an ADB USB interface. After several minutes, a forced restart returned
to the device-specific AshaOS. No guest boot log was recovered, so the precise
failure remains unknown; suppressing the automatic BPF-failure reboot was not
a fix. The diagnostic build script restored the source after building.

After recovery, both USB modes were checked: `adb devices` found the phone in
the normal system, and `fastboot devices` found it in the bootloader. This
rules out the Windows USB driver as the cause of the GSI boot failure. The
failed DSU and its separate userdata were removed from the phone; its image
remains at `C:\AshaOS\builds\GSI\diagnostic\AshaOS-GSI-Android16-arm64.img`.
Do not distribute or flash this diagnostic image as a working GSI.
