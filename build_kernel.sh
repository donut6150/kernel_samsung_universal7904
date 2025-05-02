#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

run_with_timer() {
    local start_time=$(date +%s.%N)
    "$@" 2>&1 | while IFS= read -r line; do
        now=$(date +%s.%N)
        elapsed=$(echo "$now - $start_time" | bc)
        printf "[%.6f]\t%s\n" "$elapsed" "$line"
    done
}

common() {
    run_with_timer clean
    run_with_timer defconfig "$1"
    run_with_timer kernel
    run_with_timer zip "$1" "$2"
}

clean() {
    echo "Make clean..."
    make clean -j4 2>&1 | tee log_clean.log
}

defconfig() {
    local device="$1"
    echo "Make defconfig..."
    make ARCH=arm64 -j4 "exynos7885-${device}_oneui_defconfig" 2>&1 | tee log_defconfig.log
}

kernel() {
    echo "Make kernel..."
    make ARCH=arm64 -j4 2>&1 | tee log_kernel.log
}

zip() {
    local device=$1
    local build_dtb=$2

    if [ -s "arch/arm64/boot/Image" ]; then
        echo -e "${GREEN}Build succeeded!"
        KERNEL_VERSION=$(grep UTS_RELEASE include/generated/utsrelease.h | cut -d'"' -f2 | sed 's/+*$//')
        echo -e "Kernel version: ${KERNEL_VERSION}"
        
        if [ ! -d "AnyKernel3" ]; then
            echo "AnyKernel3: No such file or directory"
            echo -e "${RED}Build failed, reason should be above this message${NC}"
            return 1
        fi
        
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
        
        if [ "$build_dtb" == "dtb" ]; then
            echo -e "Building DTB..."
            make ARCH=arm64 -j4 dtb.img 2>&1 | tee log_dtb.log
            cp arch/arm64/boot/dtb.img AnyKernel3/dtb.img
            FILES+=("dtb.img")
        fi
        
        ZIPNAME="$(tr '[:lower:]' '[:upper:]' <<< ${device:0:1})${device:1} ${KERNEL_VERSION}.zip"
        
        cd AnyKernel3/ && zip -r9 "${ZIPNAME}" "${FILES[@]}" && mv "${ZIPNAME}" ../ && cd ..
        
        echo -e "Kernel zip: $PWD/${ZIPNAME}.zip${NC}"
        echo -e "${GREEN}Make clean..."
        make clean > /dev/null
        echo -e "Done${NC}"
    else
        echo -e "${RED}Build failed, reason should be above this message${NC}"
    fi
}

# Script starts here
DEVICE="$1"
BUILD_DTB="$2"

if [ -z "$DEVICE" ]; then
    echo -e "${RED}Usage: $0 <a10,a20,a20e,a30,a30s,a40> [dtb], dtb is optional${NC}"
    echo -e "Examples:\n1) ./build_kernel.sh a30s dtb\t(Build for A30s along with DTB)"
    echo -e "2) ./build_kernel.sh a30s\t(Build for A30s without DTB)"
    exit 1
fi

echo "Current directory is $PWD"
echo "Starting build..."

echo "Device: ${DEVICE}"
echo "Build DTB: ${BUILD_DTB:-No}"
common