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

LOCAL_MODULE := libktpcm
#LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := src/knowles_tunnel_pcm.c
LOCAL_C_INCLUDES += $(LOCAL_PATH)/inc \
		    $(LOCAL_PATH)/../voiceprocessing-hal \
		    device/amlogic/yukawa/kernel-headers/uapi/linux/mfd/adnc
LOCAL_SHARED_LIBRARIES := libodsp libtunnel
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libvqhal
#LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := src/vq_hal.c \
		   src/uevent.c \
                   src/algo_plugin.c \
                   src/vq_register.c \
		   src/mixer_utils.c

LOCAL_C_INCLUDES += $(LOCAL_PATH)/inc \
		    $(LOCAL_PATH)/../voiceprocessing-hal \
		    external/tinyalsa/include \
		    device/amlogic/yukawa/kernel-headers/uapi/linux/mfd/adnc
LOCAL_SHARED_LIBRARIES := libodsp libtunnel libtinyalsa libktpcm liblog
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := sound_trigger.primary.default
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := src/sound_trigger_hw_iaxxx.c \
		   src/hotword_stream.c
LOCAL_VENDOR_MODULE := true
LOCAL_C_INCLUDES += $(LOCAL_PATH)/inc \
			device/amlogic/yukawa//kernel-headers/uapi/linux/mfd/adnc
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_SHARED_LIBRARIES := liblog \
			libvqhal  \
			libktpcm

LOCAL_CFLAGS += -Wall -Werror

ifneq (,$(findstring $(PLATFORM_VERSION), P))
LOCAL_PROPRIETARY_MODULE := true
LOCAL_CFLAGS += -DANDROID_P
endif

include $(BUILD_SHARED_LIBRARY)
