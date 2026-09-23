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

# Preserve the complete, known-working ARM32 GSI product configuration.
$(call inherit-product, device/generic/common/gsi_arm.mk)

# AshaOS product identity. PRODUCT_DEVICE remains "generic" through gsi_arm.
PRODUCT_NAME := ashaos_arm
PRODUCT_BRAND := AshaOS
PRODUCT_MODEL := AshaOS on ARM

# AshaOS-owned applications included in the GSI system image.
PRODUCT_PACKAGES += AudioAsha NavigationBarMode2ButtonOverlay

# AshaOS-owned product overlays. v0.5 simplifies the default SystemUI QS panel
# without forking SystemUI source or replacing the normal notification shade.
PRODUCT_PACKAGE_OVERLAYS += device/ashaos/overlay
