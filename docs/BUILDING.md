# Сборка AshaOS Beta 1.7

Исходная база этого релиза — LineageOS 23.2 / Android 16. Для сборки нужны
Linux или Ubuntu в WSL2, `repo`, JDK и пакеты, требуемые LineageOS. На рабочей
машине с 13 ГБ ОЗУ сборка выполнялась с `-j1`/`-j2`; для дерева Android и
промежуточных файлов разумно иметь около 500 ГБ свободного места. Держите
исходник в файловой системе Linux, а не на `/mnt/c`.

Перед очисткой рабочего WSL сохранены [ревизии проектов](../manifests/lineage-23.2-rebuild.xml)
и [список деревьев Pixel 5](../manifests/lineage-23.2-roomservice.xml).
В первом файле Bluetooth и `frameworks/base` указаны в исходных ревизиях,
чтобы к ним применились патчи AshaOS ниже. Это маленькие файлы для повторного
получения исходников, а не сами исходники или проприетарные `vendor`-файлы.

## Исходник

```sh
mkdir -p ~/android/lineage-23.2
cd ~/android/lineage-23.2
repo init -u https://github.com/LineageOS/android.git -b lineage-23.2
repo sync -c -j4
```

Клонируйте репозиторий AshaOS отдельно и примените его изменения:

```sh
cd /path/to/AshaOS
bash tools/apply-to-lineage.sh ~/android/lineage-23.2
```

Скрипт копирует `device/ashaos` и накладывает два небольших патча. Патч
Bluetooth добавляет приём Direct TCP/UDP на интерфейсах USB-модема, Wi-Fi и
Bluetooth PAN перед штатным ASHA-кодировщиком. Патч framework восстанавливает
вызов старого API Pixel 5, который нужен его системной службе теплового режима.
Базовые ревизии, на которых патчи проверены:

| Проект | База LineageOS 23.2 |
| --- | --- |
| `packages/modules/Bluetooth` | `9480eafd14cad496384f9ff21c9f22233a812663` |
| `frameworks/base` | `c8e9a4a21efb16c923371a740b9c1c755d17ebb2` |

`git apply --check` остановит работу, если обновлённый исходник несовместим с
патчем. Не заменяйте файлы Bluetooth целиком из старой сборки.

## Pixel 5 (`redfin`)

Нужны совместимые деревья `device/google/redfin`, `device/google/redbull`,
ядро Pixel 5 и проприетарные компоненты `vendor` для выбранной версии устройства.
Они не входят в этот репозиторий. В установленном дереве LineageOS:

```sh
cd ~/android/lineage-23.2
source build/envsetup.sh
lunch lineage_ashaos_redfin-bp4a-userdebug
m otapackage -j1
```

Результат находится в `out/target/product/redfin/`. Собранный ZIP предназначен
только для этой модели. Не используйте его на других Pixel или Samsung.

## ARM64 GSI

```sh
cd ~/android/lineage-23.2
source build/envsetup.sh
lunch lineage_gsi_ashaos_arm64-bp4a-userdebug
m systemimage -j1
```

Артефакт `out/target/product/generic_arm64/system.img` — только системный
раздел. При публикации проверьте, что образ не содержит `ro.bpf.kver_override`
для Pixel 5. Скрипт `device/ashaos/tools/build-gsi-arm64.sh` также проверяет
наличие приложения и Bluetooth APEX и создаёт gzip-копию с SHA-256.
Готовый образ этого релиза прошёл проверку размера, gzip и SHA-256, но не
загружался на реальном устройстве.

## Другая модель

Одного переименования драйвера недостаточно. Для каждой модели нужны
совместимые Android 16 дерево устройства, ядро, `vendor`-файлы, конфигурация
разделов и рабочий ASHA/Bluetooth стек. Создайте продукт по образцу
`lineage_ashaos_redfin.mk`, наследуйте `lineage_<codename>.mk` своей модели,
добавьте `AudioAsha` и overlay, затем проверьте сборку, загрузку, связь,
звук, восстановление и OTA. Не публикуйте OTA для модели до испытания на ней.

## Клиенты для ПК

Windows-клиент можно собрать MinGW-w64 или Visual Studio через
`cmake -S clients/windows -B build/windows` и `cmake --build build/windows`.
В Linux установите `qt6-base-dev`, `cmake`, компилятор C++, `pulseaudio-utils`
и `android-tools-adb`, затем:

```sh
cmake -S clients/linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
```

Windows использует WASAPI, Linux — монитор PulseAudio/PipeWire. Приложение
на телефоне и оба клиента в этом выпуске имеют версию 1.7.
