# AshaOS Beta 1.7

[Русский](README.md) · [Downloads](../../releases)

AshaOS is an experimental Android 16 / LineageOS 23.2 build for ASHA hearing
aids. This repository contains the Audio-Asha phone app, Windows and Linux
senders, Pixel 5 and ARM64 GSI product files, and small patches against the
LineageOS Bluetooth module and Android framework. Download binaries from the
**AshaOS Beta 1.7** release rather than from Git history.

**Status: Beta.** Problem is micro interruptions with sound on AshaOS. Though on Windows less interruptions, but on linux more interruptions. GSI didn't on Pixel 5

## Downloads

The [release](../../releases) contains:

| Asset | Purpose |
| --- | --- |
| `lineage-23.2-20260923-UNOFFICIAL-ashaos_redfin-direct-network.zip` | Android 16 OTA for an existing compatible AshaOS installation on Pixel 5 (`redfin`) |
| `boot.img`, `dtbo.img`, `vendor_boot.img` | Pixel 5 partitions from this OTA; not complete firmware |
| `windows-ashaos-1.7.exe` | Windows desktop audio sender |
| `ashaos-linux_1.7.0_amd64.deb` | Ubuntu/Debian AMD64 desktop audio sender |
| `AshaOS-GSI-Android16-arm64.img.gz` | Experimental ARM64 system image |
| `SHA256SUMS.txt` | Download hashes |

GitHub release assets must each be under 2 GiB, so the 2.38 GB raw GSI is
distributed as a 1.21 GB gzip file. The 1.27 GB OTA also fits. Binaries are
not stored in the Git repository.

The three standalone partition images are also included in the OTA. Their
sizes and SHA-256 values match its `payload.bin` manifest. Do not mix them
with another build or flash them alone as a full system installation.

## How Direct works

The PC captures desktop audio and sends 16 kHz stereo PCM records to the
**phone** using TCP or UDP over USB tethering, Wi-Fi, or Bluetooth PAN. AshaOS
adds a bounded queue at the input of Android's existing ASHA Bluetooth encoder.
This avoids feeding the PC stream through Android AudioTrack, while the system
ASHA session remains active. Android still handles G.722 encoding, pairing,
hearing-aid control, L2CAP, Bluetooth scheduling, and link encryption. AshaOS
does not install firmware on hearing aids or implement a custom Nordic-style
GATT service. Bluetooth PAN is only the network transport between PC and phone;
it shares the same radio with outgoing ASHA and may perform poorly.

An authorized ADB connection reads a fresh session nonce from
`dumpsys bluetooth_manager` for a Direct session check. Audio travels
over the selected local network, not through ADB or a cloud server. The three
legacy UDP / AudioTrack modes remain available. Match the phone mode and its
IP:port in the sender. New installs default to Direct TCP, an active desktop
audio engine, 20 ms phone buffer, 40 ms sender preroll, and −80 dBFS silence
threshold. Help text reflects the current buffer values.

Latency depends on the network, phone and hearing aids. The author's informal
observation is that Wi-Fi or Bluetooth can add around 50 ms versus USB. This
is not a guaranteed measurement or a validated GSI result.

## Install

(hmm.. usually at first "adb reboot bootloader" then unlock, install boot and dtbo and vendor_boot) 
Chatgpt -> **Pixel 5 OTA:** Only update a compatible existing AshaOS installation on
`redfin`. Back up your data and charge the phone. Enter AshaOS recovery and
select **Apply update → Apply from ADB**, then run:

```sh
adb sideload lineage-23.2-20260923-UNOFFICIAL-ashaos_redfin-direct-network.zip
```

Choose **Reboot system now** only after recovery reports success. This is not
a verified migration path from stock Android or another ROM. Stop if signature
verification fails; do not wipe data to work around that error.

**Windows:** Run `windows-ashaos-1.7.exe` and install the official
[Android Platform Tools](https://developer.android.com/tools/releases/platform-tools)
so that `adb devices` shows an authorized phone. Match the phone's network mode
and IP:port. Closing the window keeps the sender running in the tray.

**Linux:** Install with `sudo apt install ./ashaos-linux_1.7.0_amd64.deb`.
The sender captures the monitor of a selected PipeWire/PulseAudio output.
The package has not yet been tested with real hearing aids.

**GSI via DSU:** This image contains only the system partition. Confirm ARM64,
Treble, dynamic partitions, Android 16 vendor compatibility, and a recovery
route before testing. On a device that accepts a custom DSU image, use the
[official DSU procedure](https://developer.android.com/topic/dsu):

```sh
adb push AshaOS-GSI-Android16-arm64.img.gz /storage/emulated/0/Download/
adb shell am start-activity \
  -n com.android.dynsystem/com.android.dynsystem.VerificationActivity \
  -a android.os.image.action.START_INSTALL \
  -d file:///storage/emulated/0/Download/AshaOS-GSI-Android16-arm64.img.gz \
  --el KEY_SYSTEM_SIZE 2381762560 \
  --el KEY_USERDATA_SIZE 8589934592
```

DSU may reject an image not signed by a key trusted by the device, or the
guest may fail to boot. Pixel 5 testing of the previous image failed. Do not
permanently flash this GSI over your only working installation without a
tested recovery plan. There is no universal fastboot sequence; consult the
[AOSP GSI guidance](https://source.android.com/docs/core/tests/vts/gsi) and
your device's instructions.

## Build and porting

[Build instructions](docs/BUILDING.md) cover LineageOS 23.2, the two source
patches, Pixel 5 OTA and ARM64 GSI builds. Other devices require their own
compatible device tree, kernel, proprietary vendor files and testing; changing
a driver name alone is insufficient. The desktop clients build independently.

## License and components

Original AshaOS contributions use [Apache License 2.0](LICENSE) and retain
XLreal's copyright notice in [NOTICE](NOTICE.md). Upstream Android and
LineageOS files retain their own copyright and licenses. Google/Qualcomm
proprietary vendor files are not included. See [components and services](docs/COMPONENTS.md)
for why ADB, Android Bluetooth, and desktop audio services are used.
