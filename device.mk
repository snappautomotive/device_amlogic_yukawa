#LOCAL_KERNEL := device/amlogic/yukawa-kernel/Image.gz
#PRODUCT_COPY_FILES += $(LOCAL_KERNEL):kernel

# Build and run only ART
PRODUCT_RUNTIMES := runtime_libart_default

DEVICE_PACKAGE_OVERLAYS := device/amlogic/yukawa/overlay

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/fstab.yukawa:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.yukawa