#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'
DEVICE=$1
DTB=$2
DTB_YN=$([[ "$DTB" == "dtb" ]] && echo " with DTB" || echo " without DTB")

run_with_timer() {
    local start_time=$(date +%s.%N)
    "$@" 2>&1 | while IFS= read -r line; do
        now=$(date +%s.%N)
        elapsed=$(echo "$now - $start_time" | bc)
        printf "[%.6f]\t%s\n" "$elapsed" "$line"
    done
}

run_with_timer echo "Current directory is $PWD"
run_with_timer echo "Building for ${DEVICE}${DTB_YN}"

run_with_timer echo "Make clean..."
run_with_timer make clean -j4 2>&1 | tee log_clean.log

run_with_timer echo "Make defconfig..."
run_with_timer make ARCH=arm64 -j4 "exynos7885-${DEVICE}_oneui_defconfig" 2>&1 | tee "log_${DEVICE}_defconfig.log"

run_with_timer echo "Make kernel..."
run_with_timer make ARCH=arm64 -j4 2>&1 | tee log_${DEVICE}_kernel.log

if [ -s "arch/arm64/boot/Image" ]; then
    
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
    
    ZIPNAME="$(tr '[:lower:]' '[:upper:]' <<< ${DEVICE:0:1})${DEVICE:1} ${KERNEL_VERSION}.zip"
    cd AnyKernel3/ && zip -r9 "${ZIPNAME}" "${FILES[@]}" && mv "${ZIPNAME}" ../ && cd ..
    echo -e "Kernel zip: $PWD/A30s ${KERNEL_VERSION}.zip${NC}"
  
    echo -e "${GREEN}Make clean..."
    make clean > /dev/null
    echo -e "Done${NC}"
else
    echo -e "${RED}Build failed, reason should be above this message${NC}"
fi