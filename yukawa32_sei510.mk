$(call inherit-product, device/amlogic/yukawa/device-yukawa_sei510.mk)
$(call inherit-product, device/amlogic/yukawa/yukawa-common.mk)

PRODUCT_NAME := yukawa32_sei510
PRODUCT_DEVICE := yukawa32_sei510

BOARD_KERNEL_DTB := device/amlogic/yukawa-kernel/meson-g12a-sei510.dtb
PRODUCT_COPY_FILES +=  $(LOCAL_DTB):meson-g12a-sei510.dtb
