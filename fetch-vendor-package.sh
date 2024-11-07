#!/bin/bash
# fetch, check & extract the current vendor package
set -e

EXPECTED_YUKAWA_VENDOR_VERSION=20241203
EXPECTED_YUKAWA_VENDOR_SHA=e24bc1bdcc6813162295f259b8713f075a26181681afcc16d776a938829d3e11cbbb2714767b0d0fd9d84e0a6129e7d3302993df96160b822efa0a4512dfd057

DIR_PARENT=$(cd $(dirname $0); pwd)
if [ -z "${ANDROID_BUILD_TOP}" ]; then
    ANDROID_BUILD_TOP=$(cd ${DIR_PARENT}/../../../; pwd)
fi

VND_PKG_URL=https://public.amlogic.binaries.baylibre.com/ci/vendor_packages/${EXPECTED_YUKAWA_VENDOR_VERSION}/extract-yukawa_devices-${EXPECTED_YUKAWA_VENDOR_VERSION}.tgz
PKG_FILE=extract-yukawa_devices-${EXPECTED_YUKAWA_VENDOR_VERSION}

pushd ${ANDROID_BUILD_TOP}

# remove the older vendor-package, if any
rm -rf ${ANDROID_BUILD_TOP}/vendor/amlogic/yukawa

if [ ! -e "${PKG_FILE}.tgz"  ]; then
    echo "Vendor package not present: fetching it"
    curl -L ${VND_PKG_URL} -o  ${PKG_FILE}.tgz
fi

# verify checksum
echo "${EXPECTED_YUKAWA_VENDOR_SHA} ${PKG_FILE}.tgz" | sha512sum -c
if [ $? -ne 0 ]; then
    echo "Vendor package checksum mismatch: abort"
    exit 1
fi

tar -xf ${PKG_FILE}.tgz
./${PKG_FILE}.sh
popd
