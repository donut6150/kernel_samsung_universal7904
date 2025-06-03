#!/bin/bash

start_time=$(date +%s)

set -euo pipefail
[[ -n "${3:-}" ]] && set "-$3"
trap "echo KeyboardInterrupt!; exit 1" INT

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'
DEVICE=$1
DTB=$2
DTB_YN=$([[ "$DTB" == "dtb" ]] && echo " with DTB" || echo " without DTB")
ROOT_DIR=$(pwd)

cd KernelSU-Next/
CHASH=$(git show next --format=%h -s)
cd ..

LOCALVER="-$(cat .version)-KSUN@${CHASH}"

clean() {
    cd ${ROOT_DIR}/
    echo "Current directory is $PWD"
    echo "Make clean..."
    make clean -j4 2>&1 | tee log_clean.log
    rm -rf include/generated/ include/config/
}

defconfig() {
    echo "Make defconfig..."
    make LOCALVERSION=\"${LOCALVER}\" ARCH=arm64 -j4 "exynos7885-${DEVICE}_oneui_defconfig" 2>&1 | tee "log_${DEVICE}_defconfig.log"
}

kernel() {
    echo "Make kernel..."
    make LOCALVERSION=\"${LOCALVER}\" ARCH=arm64 -j4 2>&1 | tee log_${DEVICE}_kernel.log
}

zip_kernel() {
    echo -e "${GREEN}Build succeeded!"
    KERNEL_VERSION=$(grep UTS_RELEASE include/generated/utsrelease.h | cut -d'"' -f2 | sed 's/+*$//')
    echo -e "Kernel version: ${KERNEL_VERSION}"
    
    cp arch/arm64/boot/Image AnyKernel3/Image
    FILES=(
        "Image"
        "version"
        "META-INF/com/google/android/update-binary"
        "META-INF/com/google/android/updater-script"
        "tools/ak3-core.sh"
        "tools/busybox"
        "tools/magiskboot"
        "tools/tweaks.zip"
        "anykernel.sh"
    )
    
    if [ "${DTB}" == "dtb" ]; then
        echo -e "Building DTB..."
        make ARCH=arm64 -j4 dtb.img 2>&1 | tee log_dtb.log
        cp arch/arm64/boot/dtb.img AnyKernel3/dtb.img
        FILES+=("dtb.img")
    fi
    
    sed -i "1s/.*/${KERNEL_VERSION}/" AnyKernel3/version

    ZIPNAME="$(tr '[:lower:]' '[:upper:]' <<< ${DEVICE:0:1})${DEVICE:1} ${KERNEL_VERSION}.zip"

    echo "Current directory is $PWD"

    cd AnyKernel3/ 
    zip -r9 "${ZIPNAME}" "${FILES[@]}"
    mv "${ZIPNAME}" "${ROOT_DIR}/"
    cd ${ROOT_DIR}/

    echo -e "Kernel zip: $PWD/${ZIPNAME}${NC}"
    echo -e "Done${NC}"

    sed -i "1s/.*/KERNELVERSION/" ${ROOT_DIR}/AnyKernel3/version
}

main() {
    echo "KSUN commit hash: ${CHASH}"
    echo "Local version: ${LOCALVER}"
    clean
    defconfig
    kernel
    if [ -s "arch/arm64/boot/Image" ]; then
        zip_kernel
    else
        echo -e "${RED}Build failed, reason should be above this message${NC}"
    fi
    clean
    end_time=$(date +%s)
    elapsed_time=$((end_time - start_time))
    echo -e "Took $(printf '%02d minutes, %02d seconds' $((elapsed_time%3600/60)) $((elapsed_time%60)))"
}

main