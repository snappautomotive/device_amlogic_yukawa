#!/usr/bin/env bash

set -o errexit
set -o pipefail
set -o nounset

set -o xtrace

# The goal of this script is gather all binaries provides by AML in order to generate
# our final u-boot image from the u-boot.bin (bl33)
#
# Some binaries come from the u-boot vendor kernel (bl21, acs, bl301)
# Others from the buildroot package (aml_encrypt tool, bl2.bin, bl30)

function usage() {
    echo "Usage: $0 [openlinux branch] [soc] [refboard]"
}

if [[ $# -lt 3 ]]
then
    usage
    exit 22
fi

GITBRANCH=${1}
# soc family: should gxl or axg
SOCFAMILY=${2}
REFBOARD=${3}

if [[ "$SOCFAMILY" == "gxm" ]]
then
    SOCFAMILY="gxl"
fi

if [[ "$SOCFAMILY" == "sm1" ]]
then
    SOCFAMILY="g12a"
fi

if ! [[ "$SOCFAMILY" == "gxl" || "$SOCFAMILY" == "axg" || "$SOCFAMILY" == "g12a" || "$SOCFAMILY" == "g12b" || "$SOCFAMILY" == "sm1" ]]
then
    echo "${SOCFAMILY} is not supported - should be [gxl, gxm, axg, g12a, g12b, sm1]"
    usage
    exit 22
fi

BIN_LIST="p-amlogic/firmware/bin/bl2:$SOCFAMILY/bl2.bin \
	  p-amlogic/firmware/bin/bl30:$SOCFAMILY/bl30.bin \
	  p-amlogic/firmware/bin/bl31:$SOCFAMILY/bl31.img \
	  p-amlogic/amlogic/tools/fip:$SOCFAMILY/aml_encrypt_$SOCFAMILY"

if [[ "$SOCFAMILY" == "gxl" || "$SOCFAMILY" == "axg" ]]
then
    BIN_LIST="$BIN_LIST p-amlogic/amlogic/tools/fip:acs_tool.pyc"
elif [[ "$SOCFAMILY" == "g12a"  || "$SOCFAMILY" == "g12b" || "$SOCFAMILY" == "sm1" ]]
then
    BIN_LIST="$BIN_LIST p-amlogic/amlogic/tools/fip:$SOCFAMILY/*.fw"
fi

# path to clone the openlinux repos
# TMP_GIT=$(mktemp -d)
TMP_GIT=$PWD/"build-collect-${SOCFAMILY}-${REFBOARD}-${GITBRANCH}-$(date +%Y%m%d-%H%M%S)"
TMP="fip-collect-${SOCFAMILY}-${REFBOARD}-${GITBRANCH}-$(date +%Y%m%d-%H%M%S)"
mkdir $TMP
mkdir $TMP_GIT

# U-Boot
git clone --depth=2 https://gitlab.com/baylibre/amlogic/atv/u-boot.git -b fip/sm1_sei610/$GITBRANCH $TMP_GIT/u-boot
mkdir $TMP_GIT/gcc-linaro-aarch64-none-elf
wget -qO- https://releases.linaro.org/archive/13.11/components/toolchain/binaries/gcc-linaro-aarch64-none-elf-4.8-2013.11_linux.tar.xz | tar -xJ --strip-components=1 -C $TMP_GIT/gcc-linaro-aarch64-none-elf
mkdir $TMP_GIT/gcc-linaro-arm-none-eabi
wget -qO- https://releases.linaro.org/archive/13.11/components/toolchain/binaries/gcc-linaro-arm-none-eabi-4.8-2013.11_linux.tar.xz | tar -xJ --strip-components=1 -C $TMP_GIT/gcc-linaro-arm-none-eabi
sed -i "s,/opt/gcc-.*/bin/,," $TMP_GIT/u-boot/Makefile
(
    cd $TMP_GIT/u-boot
    make ${REFBOARD}_defconfig
    PATH=$TMP_GIT/gcc-linaro-aarch64-none-elf/bin:$TMP_GIT/gcc-linaro-arm-none-eabi/bin:$PATH make > /dev/null
)
cp $TMP_GIT/u-boot/build/board/amlogic/*/firmware/acs.bin $TMP/
cp $TMP_GIT/u-boot/build/scp_task/bl301.bin $TMP/

# FIP/BLX
for item in $BIN_LIST
do
    REPO=$(echo $item | cut -d: -f1)
    BIN=$(echo $item | cut -d: -f2)
    DIR=$TMP_GIT/$(basename $REPO)
    BRANCH=$GITBRANCH

    if [[ ! -d $DIR ]]
    then
    # AXG & co uses the 1.3 variant of bl31
    if [[ "$REPO" == "p-amlogic/firmware/bin/bl31" ]] && ! [[ "$SOCFAMILY" == "gxl" ]]
    then
        if [[ -n $(echo $BRANCH | grep amlogic-dev) ]]
        then
            BRANCH=$(echo $BRANCH | sed "s/amlogic-dev/amlogic-dev-1.3/")
        else
        	BRANCH="${BRANCH}_1.3"
        fi
    fi

        git clone --depth=2 git@openlinux.amlogic.com:$REPO -b $BRANCH $DIR
    fi

    cp $DIR/$BIN ${TMP}
done

# Normalize
mv $TMP/aml_encrypt_$SOCFAMILY $TMP/aml_encrypt

date > $TMP/info.txt
echo "SOC: $SOCFAMILY" >> $TMP/info.txt
echo "BRANCH: $GITBRANCH ($(date +%Y%m%d))" >> $TMP/info.txt
for component in $TMP_GIT/*
do
    if [[ -d $component/.git ]]
    then
        echo "$(basename $component): $(git --git-dir=$component/.git log --pretty=format:%H HEAD~1..HEAD)" >> $TMP/info.txt
    fi
done
echo "BOARD: $REFBOARD" >> $TMP/info.txt
echo "export SOCFAMILY=$SOCFAMILY" > $TMP/soc-var.sh
