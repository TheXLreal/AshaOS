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

# Retain LineageOS's Android 16 ARM64 GSI partition layout and packages.
$(call inherit-product, vendor/lineage/build/target/product/lineage_gsi_arm64.mk)

# The generic board maps product to system/product, so AudioAsha is carried
# inside the flashable system.img even though the app is product-specific.
PRODUCT_PACKAGES += \
    AudioAsha \
    NavigationBarMode2ButtonOverlay

PRODUCT_PACKAGE_OVERLAYS += device/ashaos/overlay

PRODUCT_NAME := lineage_gsi_ashaos_arm64
PRODUCT_DEVICE := generic_arm64
PRODUCT_BRAND := AshaOS
PRODUCT_MODEL := AshaOS GSI on ARM64
PRODUCT_MANUFACTURER := AshaOS

# Do not set a device-specific BPF kernel override in the generic image.
# A Pixel 5 experiment needs its own variant so other devices keep their
# actual kernel feature detection.
