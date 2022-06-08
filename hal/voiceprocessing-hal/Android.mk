# Copyright (C) 2018 Knowles Electronics
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

LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)

LOCAL_MODULE := libvoiceprocessing
#LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := voice_processing_hw.c

LOCAL_C_INCLUDES += external/tinyalsa/include \
		    device/amlogic/yukawa/kernel-headers/uapi/linux/mfd/adnc \
		    device/amlogic/yukawa/kernel-headers
LOCAL_SHARED_LIBRARIES := liblog libutils libcutils libtinyalsa
LOCAL_PROPRIETARY_MODULE := true
# LOCAL_32_BIT_ONLY := true

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libodsp
#LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_C_INCLUDES += device/amlogic/yukawa/kernel-headers/uapi/linux/mfd/adnc
LOCAL_SRC_FILES := iaxxx_odsp_hw.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libtunnel
#LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_C_INCLUDES += device/amlogic/yukawa/kernel-headers/uapi/linux/mfd/adnc
LOCAL_SRC_FILES := tunnel.c
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)
