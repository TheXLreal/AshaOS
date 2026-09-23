# AshaOS for Linux

This desktop client captures the selected Linux audio output through the
PipeWire/PulseAudio compatibility service and sends it to the existing
Audio-Asha app on the phone. Its mode selector matches all nine phone modes:

- Direct TCP or UDP on USB, Wi-Fi, or Bluetooth PAN: session-tagged 16 kHz PCM
  records sent straight to the AshaOS Bluetooth path. Each Direct mode needs
  an authorized `adb` connection to read the current session nonce.
- UDP / AudioTrack on Wi-Fi, USB tethering, or Bluetooth PAN: 48 kHz packets
  through the regular Android audio output.

It does not require a different phone APK or another ROM build. Direct mode
still requires a phone already running an AshaOS system image that contains the
direct Bluetooth path.

## Ubuntu, Debian, Linux Mint

For the packaged 64-bit build:

```bash
sudo apt install ./ashaos-linux_1.7.0_amd64.deb
```

To build from source instead:

```bash
sudo apt install build-essential cmake qt6-base-dev pulseaudio-utils android-tools-adb
./install.sh
```

The program is installed as `ashaos-linux` and appears as **AshaOS** in the
application menu. Closing the window hides it in the desktop tray when a tray
is available. Use **Quit completely** from the tray menu to stop and exit.

## Use

1. On the phone, open Audio-Asha, choose a connection mode, and press Start.
2. Enable the matching network connection and copy the phone's IPv4 address and
   port into the Linux client.
3. Choose the Linux audio output whose system mix should be captured.
4. For any Direct mode, authorize USB or wireless debugging and confirm that
   `adb devices` shows the phone as `device`.
5. Press Start. The client keeps sending authenticated silence before the first
   sound and while the desktop is idle, so Start remains active.

Defaults match the desktop design: Direct TCP, keep desktop audio active,
20 ms phone buffer, 40 ms preroll, -80 dBFS silence threshold, 0 dBFS maximum
output, and adaptive buffering off.

## Audio compatibility

The capture commands are `parec` and `pacat`. They work with PulseAudio and
with PipeWire installations that provide `pipewire-pulse`, which is the normal
desktop setup on current Ubuntu releases. If a sink monitor has a nonstandard
name, open **For developers** and enter the source shown by:

```bash
pactl list short sources
```
