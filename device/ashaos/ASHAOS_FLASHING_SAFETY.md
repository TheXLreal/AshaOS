# Safe installation and updates for Pixel 5

This port targets Google Pixel 5 only (codename `redfin`). No package for a
different device is safe to install.

## Safety model

- Verified Boot stays enabled. Development builds use public test keys; use a
  `user` build signed with private release keys for a daily-use release.
- The AshaOS OTA payload deliberately excludes bootloader, modem, and other
  low-level firmware partitions. Install the final official Android 14 Pixel 5
  firmware on both slots before installing AshaOS.
- Keep the bootloader unlocked while using development or custom-key builds.
  This preserves the ability to install another redfin-compatible OS, but it
  reduces protection against an attacker with physical access.
- Never relock the bootloader on a test-key or untested custom build.
- A/B rollback protects the boot partitions; it is not a backup of user data.
  Back up data before every OS change. Switching between unrelated operating
  systems normally requires formatting userdata.

## Validate a package

Run the validator before using an AshaOS OTA or fastboot image archive:

```sh
device/ashaos/tools/check-redfin-package.sh path/to/package.zip
```

It verifies the `redfin` device metadata, prints SHA-256, and refuses packages
containing bootloader or radio firmware images.

## Update an existing AshaOS installation

Use a full A/B OTA package. Boot the AshaOS recovery, select **Apply update from
ADB**, and run:

```sh
adb sideload out/target/product/redfin/ashaos_redfin-ota-*.zip
```

The OTA is applied to the inactive slot. Do not manually change slots during
the first boot. Android marks the new slot successful only after boot checks
finish; otherwise the bootloader can fall back to the previous slot.

## First installation

1. Back up all user data.
2. Install the final official Android 14 firmware for Pixel 5 on both slots.
3. Enable OEM unlocking and unlock the bootloader. Unlocking erases userdata.
4. Verify `fastboot getvar product` reports `redfin`.
5. Validate the AshaOS package with the script above.
6. Follow the generated package's install method. Do not flash individual
   dynamic-partition images by guesswork.

A fastboot archive must not contain `abl.img`, `xbl.img`, `modem.img`, or
other firmware listed by the validator. Firmware maintenance is intentionally
kept in Google's official factory-image workflow.

## Replacing AshaOS with another OS

Leave the bootloader unlocked, back up data, and follow the new OS project's
Pixel 5 instructions. The replacement must explicitly support `redfin`.
Factory-reset userdata when required by the new OS. Relock only after restoring
a complete official Google image and confirming both slots boot correctly.
