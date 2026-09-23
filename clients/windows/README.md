# AshaOS for Windows 1.7

This program captures the selected Windows audio endpoint and sends PCM audio to
Audio-Asha on AshaOS. The phone is now the source of truth for the connection
mode and target. The Windows program never searches for an address: copy the
IPv4 address and port shown by the phone into the two fields at the top.

## Connection modes

Choose a mode in Audio-Asha on the Pixel, enable that network connection, and
press **Start listening**. When the app says **PC target**, enter that exact
address and port in the Windows sender.

| Phone mode | Direct | Use UDP | Transport |
| --- | --- | --- | --- |
| Wi-Fi — UDP / AudioTrack | Off | — | Legacy UDP through AudioTrack |
| USB tether — UDP / AudioTrack | Off | — | Legacy UDP through AudioTrack |
| Bluetooth PAN — UDP / AudioTrack | Off | — | Legacy UDP through AudioTrack |
| Direct USB — TCP | On | Off | Direct TCP on USB tether |
| Direct USB — UDP | On | On | Direct UDP on USB tether |
| Direct Wi-Fi — TCP | On | Off | Direct TCP on Wi-Fi |
| Direct Wi-Fi — UDP | On | On | Direct UDP on Wi-Fi |
| Direct Bluetooth PAN — TCP | On | Off | Direct TCP on Bluetooth PAN |
| Direct Bluetooth PAN — UDP | On | On | Direct UDP on Bluetooth PAN |

For Wi-Fi and Bluetooth PAN, connect the PC to the matching phone network
before starting the sender. For either USB mode, enable USB tethering first.
Every Direct mode additionally needs one authorized ADB device because the sender
reads the current per-session nonce from `dumpsys bluetooth_manager`; audio does
not use an ADB tunnel.

The UDP modes use Android AudioTrack before Android routes audio to the hearing
device. AshaOS Direct supplies PCM to the ASHA stack before the ASHA
encoder, but Android still owns Bluetooth radio scheduling, L2CAP and the actual
hearing-device link.

## Direct audio controls

- **Direct buffer** is the requested phone-side jitter buffer (1-300 ms).
- **Sender preroll** is an extra PC-side reserve (0-100 ms in 10 ms steps).
  Direct records are 10 ms, so 0 means no extra reserve beyond one complete
  record. The default is 40 ms.
- **Min gate dBFS** is the idle threshold (-120 to -20, default -80). PCM below
  the threshold is replaced by silence.
- **Max output dBFS** applies attenuation only (-30 to 0, default 0). It never
  boosts the captured PCM.

Sender 1.7 transmits session-tagged silence records while the source is idle.
Start stays active before the first Windows sound and after playback ends. The
Windows audio engine is also kept active by a silent render stream by default.
Silent capture frames keep the sender queue ready, so playback can resume after
a quiet passage without waiting for preroll again. A short capture gap also
does not restart preroll. Min/Max dBFS are PC-side PCM controls; they do not
override the ASHA volume selected by Android or the hearing device.

## Basic use

1. Unlock the Pixel and open Audio-Asha.
2. Select the connection mode and enable the corresponding Wi-Fi, USB tethering,
   or Bluetooth tethering connection.
3. Press **Start listening** on the phone.
4. Copy the displayed `IP:port` into the Windows IPv4 and Port fields.
5. Check **AshaOS Direct** for any Direct phone mode. Check **Use UDP** only
   when the selected phone mode also says UDP; leave it clear for TCP.
6. Press **Start** on Windows, even if nothing is playing. The defaults are
   Direct TCP on, Windows audio engine on, Direct buffer 20 ms, Preroll 40 ms,
   Min dBFS -80,
   with adaptive buffering off. Advanced options and the readable status
   log are under **For developers**. Hover over a round question-mark icon
   to see a short explanation; click it for details. The preroll help explains
   its added delay and how 0 ms behaves.
7. Press **Stop** before changing mode or target.

The window can be resized. Closing it hides the sender in the Windows tray
without stopping audio. Double-click the tray icon to reopen it, or right-click
the icon and choose **Exit completely** to stop the stream and quit.

Use these modes only on a trusted local connection. Every Direct record carries
the current AshaOS session nonce, which is visible on the local network and
is not end-to-end encryption or a cryptographic message authentication code.
The phone binds only to the exact selected USB, Wi-Fi or Bluetooth-PAN interface
addresses. The sender does not unlock the bootloader, disable
AVB, or alter phone security settings.

## Diagnostics

After **Stop**, the sender writes `AudioAshaSender-v1.7-last.csv` beside the
executable. The filename matches the diagnostic collector.
Run `Collect-AshaDiagnostics-v1.7.ps1` to collect the sender CSV, Bluetooth state,
AudioFlinger state, network interfaces and relevant logcat lines. The collector
requires an authorized `adb` connection.

## Build with Visual Studio

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is `build\Release\AudioAshaSender.exe`.
