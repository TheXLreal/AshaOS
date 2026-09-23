#
# Copyright (C) 2026 The AshaOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# LineageOS 23.2 supplies the Android 16 platform, Pixel 5 device
# configuration, recovery, kernel, and matching proprietary files.
$(call inherit-product, device/google/redfin/lineage_redfin.mk)

# AshaOS-owned application and UI defaults.
PRODUCT_PACKAGES += \
    AudioAsha \
    NavigationBarMode2ButtonOverlay

PRODUCT_PACKAGE_OVERLAYS += device/ashaos/overlay

PRODUCT_NAME := lineage_ashaos_redfin
PRODUCT_DEVICE := redfin
PRODUCT_BRAND := AshaOS
PRODUCT_MODEL := AshaOS on Pixel 5
PRODUCT_MANUFACTURER := Google
