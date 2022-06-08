# Knowles specific make file. Knowles changes/additions should be done in
# this file. Add this file to product makefile (msm8996.mk or equivalent on
# other platforms).

# ++ Knowles implementation ++

# Add Knowles firmware and CVQ model files to system/etc/firmware/audience


# Add Knowles firmware and CVQ model files to system/etc/firmware/audience
ADNC_FIRMWARE_AND_CVQ_MODELS_SRC_DIR = $(LOCAL_PATH)/binaries/audience/ia8x01
ADNC_FIRMWARE_AND_CVQ_MODELS_DST_DIR = vendor/firmware/audience/ia8x01

PRODUCT_COPY_FILES += \
        $(foreach f,$(wildcard $(ADNC_FIRMWARE_AND_CVQ_MODELS_SRC_DIR)/*.*),$(f):$(subst $(ADNC_FIRMWARE_AND_CVQ_MODELS_SRC_DIR),$(ADNC_FIRMWARE_AND_CVQ_MODELS_DST_DIR),$(f)))

#ADNC_REF_MODELS_SRC_DIR = yukawa-kernel/firmware/audience/cvqmodels/refModel
#ADNC_REF_MODELS_DST_DIR = vendor/firmware/audience/refModel
#PRODUCT_COPY_FILES += \
        $(foreach f,$(wildcard $(ADNC_REF_MODELS_SRC_DIR)/*),$(f):$(subst $(ADNC_REF_MODELS_SRC_DIR),$(ADNC_REF_MODELS_DST_DIR),$(f)))

# Copy the mixer path xml file to the vendor folder
#PRODUCT_COPY_FILES += \
    device/linaro/hikey/audio/mixer_paths_ia8x01_hikey.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_ia8x01.xml

# Copy AVS SDK dependencies to vendor folder
#PRODUCT_COPY_FILES += \
        $(foreach f,$(wildcard $(ADNC_AVS_SDK_SRC_BIN_DIR)/*),$(f):$(subst $(ADNC_AVS_SDK_SRC_DIR),$(TARGET_COPY_OUT_VENDOR)/$(ADNC_AVS_SDK_DST_DIR)/,$(f)))
#PRODUCT_COPY_FILES += \
        $(foreach f,$(wildcard $(ADNC_AVS_SDK_SRC_LIB_DIR)/*),$(f):$(subst $(ADNC_AVS_SDK_SRC_DIR),$(TARGET_COPY_OUT_VENDOR)/$(ADNC_AVS_SDK_DST_DIR)/,$(f)))
#PRODUCT_COPY_FILES += \
        $(foreach f,$(wildcard $(ADNC_AVS_SDK_SRC_DIR)/*.*),$(f):$(subst $(ADNC_AVS_SDK_SRC_DIR),$(TARGET_COPY_OUT_VENDOR)/$(ADNC_AVS_SDK_DST_DIR)/,$(f)))

# Copy the mixer path xml file to the vendor folder
#PRODUCT_COPY_FILES += \
#   device/amlogic/yukawa/audio/mixer_paths_ia8x01_yukawa.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_ia8x01.xml

# Add VoiceQ app and related libraries
PRODUCT_PACKAGES += VoiceQMultikeyword \
                    I2sPdmRecorder \
                    libknStageTwoDetectorWrapper.so \
                    libVtArm.so \
                    libcVoiceQ.so \
                    libPocoFoundation.so \
                    libc++_shared.so \
                    libcpp_shared.so \
                    libPocoJSON.so \
                    libPocoUtil.so \
                    libPocoXML.so \
                    libkspvtpgkw.so \
                    libshptokspvt.so

# Add console apps and other required libraries
PRODUCT_PACKAGES += libvoiceprocessing \
                    libtunnel \
                    libodsp \
                    libktpcm \
                    libvqhal \
                    voice_detection \
                    adnc_strm.primary.default \
                    sound_trigger.primary.default \
                    odsp_api_test \
                    tunneling_hal_test_sensor \
                    setparamblk_test \
                    sensor_param_test \
                    tunneling_hal_test \
                    dump_debug_info \
                    tunneling_hal_test_calypso \
                    crash_event_logger \
                    spi_reliability_test \
                    script_trigger_test \
                    SoundTriggerTestApp \
                    libaudioroute \
                    ti_codec_config \
                    adb_thru_wifi \
                    adb_thru_usb \
                    tunnel_ti_codec \
                    ti_codec_read_values

# -- Knowles implementation --

