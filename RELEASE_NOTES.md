# AshaOS Beta 1.7 — Android 16

Экспериментальный выпуск для Google Pixel 5 (`redfin`) на базе LineageOS 23.2.
Телефонное приложение Audio-Asha и клиенты Windows/Linux теперь обозначены
версией 1.7. Direct-передача PCM с ПК на телефон доступна через USB-модем,
Wi-Fi и Bluetooth PAN по TCP или UDP. Предбуфер новых установок клиентов —
40 мс; подсказки показывают текущее значение буфера. Три режима
UDP/AudioTrack также сохранены.

**Что скачивать:**

| Файл | Для кого |
| --- | --- |
| `lineage-23.2-20260923-UNOFFICIAL-ashaos_redfin-direct-network.zip` | OTA для Pixel 5 с уже установленной совместимой AshaOS |
| `windows-ashaos-1.7.exe` | Windows |
| `ashaos-linux_1.7.0_amd64.deb` | Ubuntu/Debian AMD64 |
| `AshaOS-GSI-Android16-arm64.img.gz` | Экспериментальный ARM64 system image |
| `SHA256SUMS.txt` | SHA-256 для проверки файлов |

Инструкции по установке, объяснение Direct и сборки для других устройств:
[README](https://github.com/TheXLreal/AshaOS#readme) ·
[English](https://github.com/TheXLreal/AshaOS/blob/main/README.en.md) ·
[Сборка](https://github.com/TheXLreal/AshaOS/blob/main/docs/BUILDING.md).

**Известные ограничения.** OTA проверена как пакет, но обновление на живом
Pixel 5 после этого выпуска ещё не подтверждено. Linux-клиент собран, но пока
не испытан со слуховыми аппаратами. Предыдущая GSI не загрузилась через DSU
на Pixel 5; новый общий образ также не подтверждён на устройствах. GSI не
заменяет `vendor`, ядро и драйверы, не подходит автоматически для всех моделей.
Bluetooth PAN может иметь обрывы, потому что разделяет радио с ASHA. По
неформальному наблюдению автора Wi-Fi/Bluetooth добавляют примерно 50 мс
относительно USB, но это не гарантированный замер.

**Direct не прошивает слуховой аппарат.** PCM приходит по локальному TCP/UDP
в очередь модуля Bluetooth телефона до штатного ASHA-кодировщика; Android
сохраняет G.722, сопряжение, управление ASHA и передачу по Bluetooth LE. ADB
нужен для метки сеанса, а не для самого звука. Сетевой поток не имеет
сквозного шифрования, поэтому используйте доверенную локальную сеть.

Original AshaOS contributions are licensed under Apache 2.0. Existing
Android/LineageOS copyrights remain with their owners; proprietary vendor
files are not included. This is a beta release, and compatibility with other
devices must be tested individually.
