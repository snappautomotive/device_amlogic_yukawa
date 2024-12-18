#!/bin/bash
# fetch, check & extract the current vendor package
set -e

EXPECTED_YUKAWA_VENDOR_VERSION=20241213
EXPECTED_YUKAWA_VENDOR_SHA=2a1a531f64df8bc6cf2f96f786f43112f957ba4d7eb6465e34306fb86d5c2490f9cbca7300fd11a978af7c7a08c7b8707b2083d1d3e91569d4e3e9b5ffd4d50a

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
