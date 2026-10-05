#!/bin/bash

# Kernel build script for munch (POCO F4 / Redmi K40S)
# Platform: Kona (SM8250 / Snapdragon 870)

set -e

TOOLCHAIN_PATH=$HOME/zyc-clang/bin
GIT_COMMIT_ID=$(git rev-parse --short=8 HEAD 2>/dev/null || echo "custom")
TARGET_DEVICE="munch"

if [ ! -d "$TOOLCHAIN_PATH" ]; then
    echo "Lỗi: Không tìm thấy TOOLCHAIN_PATH tại [$TOOLCHAIN_PATH]."
    exit 1
fi

export PATH="$TOOLCHAIN_PATH:$PATH"

for cmd in aarch64-linux-gnu-ld arm-linux-gnueabi-ld clang; do
    if ! command -v $cmd >/dev/null 2>&1; then
        echo "Lỗi: Không tìm thấy lệnh [$cmd] trong PATH."
        exit 1
    fi
done

# Cấu hình Ccache
export CCACHE_DIR="$HOME/.cache/ccache_mikernel"
export CC="ccache gcc"
export CXX="ccache g++"
export PATH="/usr/lib/ccache:$PATH"

MAKE_ARGS="ARCH=arm64 SUBARCH=arm64 O=out CC=clang CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- CROSS_COMPILE_COMPAT=arm-linux-gnueabi- CLANG_TRIPLE=aarch64-linux-gnu-"

# Phím tắt tiếp tục hoặc build 1 luồng để check lỗi
if [ "$1" == "j1" ]; then
    make $MAKE_ARGS -j1
    exit 0
fi

if [ "$1" == "continue" ]; then
    make $MAKE_ARGS -j$(nproc)
    exit 0
fi

# Tùy chọn SukiSU + SUSFS
KSU_ZIP_STR="NoKernelSU"
KSU_ENABLE=0
if [ "$1" == "ksu" ] || [ "$2" == "ksu" ]; then
    KSU_ENABLE=1
    KSU_ZIP_STR="SukiSU-SUSFS"
fi

echo "=========================================="
echo " Thiết bị target : $TARGET_DEVICE"
echo " KernelSU/SUSFS  : $KSU_ZIP_STR"
echo " Trình biên dịch : $(clang --version | head -n 1)"
echo "=========================================="

# Cài đặt mã nguồn SukiSU-Ultra trực tiếp từ repo gốc
if [ $KSU_ENABLE -eq 1 ]; then
    echo ">> Đang kéo mã nguồn SukiSU-Ultra (main)..."
    curl -LSs "https://raw.githubusercontent.com/SukiSU-Ultra/SukiSU-Ultra/refs/heads/main/kernel/setup.sh" | bash -s main
fi

# Dọn dẹp thư mục build
rm -rf out/ anykernel/

# Clone AnyKernel3 nhánh kona
git clone https://github.com/liyafe1997/AnyKernel3 -b kona --single-branch --depth=1 anykernel

DEFCONFIG_AOSP="munch_defconfig"
DEFCONFIG_MIUI="munch_stock-defconfig"

# Cập nhật Local Version
local_version_str="-perf"
local_version_date_str="-munch-$(date +%Y%m%d)-${GIT_COMMIT_ID}-perf"
[ -f "arch/arm64/configs/${DEFCONFIG_AOSP}" ] && sed -i "s/${local_version_str}/${local_version_date_str}/g" "arch/arm64/configs/${DEFCONFIG_AOSP}"
[ -f "arch/arm64/configs/${DEFCONFIG_MIUI}" ] && sed -i "s/${local_version_str}/${local_version_date_str}/g" "arch/arm64/configs/${DEFCONFIG_MIUI}"

# Hàm cấu hình KSU và SUSFS
apply_ksu_susfs_config() {
    if [ $KSU_ENABLE -eq 1 ]; then
        scripts/config --file out/.config \
            -e KSU \
            -e KSU_MANUAL_HOOK \
            -e KSU_SUSFS \
            -e KSU_SUSFS_SUS_PATH \
            -e KSU_SUSFS_SUS_MOUNT \
            -e KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT \
            -e KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT \
            -e KSU_SUSFS_SUS_KSTAT \
            -e KSU_SUSFS_SUS_MAP \
            -e KSU_SUSFS_TRY_UMOUNT \
            -e KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT \
            -e KSU_SUSFS_SPOOF_UNAME \
            -e KSU_SUSFS_ENABLE_LOG \
            -e KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS \
            -e KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG \
            -e KSU_SUSFS_OPEN_REDIRECT \
            -e THREAD_INFO_IN_TASK \
            -e KPM
    else
        scripts/config --file out/.config -d KSU
    fi
}

# Hàm patch KPM cho SukiSU
patch_kpm_image() {
    if [ $KSU_ENABLE -eq 1 ]; then
        echo ">> Đang chạy patch_linux cho Image..."
        cd out/arch/arm64/boot/
        wget -q https://github.com/SukiSU-Ultra/SukiSU_KernelPatch_patch/releases/latest/download/patch_linux -O patch_linux
        chmod +x patch_linux
        ./patch_linux
        rm Image
        mv oImage Image
        rm patch_linux
        cd -
    fi
}

# ====================================================
# ------------- BUILD CHO AOSP ROM -------------------
# ====================================================
echo ">> [1/2] Build kernel AOSP (sử dụng $DEFCONFIG_AOSP)..."
make $MAKE_ARGS $DEFCONFIG_AOSP
apply_ksu_susfs_config
make $MAKE_ARGS -j$(nproc)

if [ ! -f "out/arch/arm64/boot/Image" ]; then
    echo "Lỗi: Không tìm thấy out/arch/arm64/boot/Image cho AOSP!"
    exit 1
fi

find out/arch/arm64/boot/dts/vendor/qcom/ -name '*kona*.dtb' -exec cat {} + > out/arch/arm64/boot/dtb 2>/dev/null || \
find out/arch/arm64/boot/dts/ -name '*.dtb' -exec cat {} + > out/arch/arm64/boot/dtb

patch_kpm_image

rm -rf anykernel/kernels/ && mkdir -p anykernel/kernels/
cp out/arch/arm64/boot/Image anykernel/kernels/
cp out/arch/arm64/boot/dtb anykernel/kernels/

cd anykernel
ZIP_AOSP="Kernel_AOSP_munch_${KSU_ZIP_STR}_$(date +'%Y%m%d_%H%M%S')_${GIT_COMMIT_ID}.zip"
zip -r9 "$ZIP_AOSP" ./* -x .git .gitignore out/ ./*.zip
mv "$ZIP_AOSP" ../
cd ..
echo ">> Build AOSP hoàn tất: $ZIP_AOSP"


# ====================================================
# ------------- BUILD CHO MIUI / HYPEROS -------------
# ====================================================
echo ">> [2/2] Build kernel MIUI/HyperOS (sử dụng $DEFCONFIG_MIUI)..."
rm -rf out/

dts_source=arch/arm64/boot/dts/vendor/qcom
cp -a ${dts_source} .dts.bak

# Tinh chỉnh panel l11r cho POCO F4 / Redmi K40S (munch)
sed -i 's/<155>/<1546>/g' ${dts_source}/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi 2>/dev/null || true
sed -i 's/<70>/<695>/g'   ${dts_source}/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi 2>/dev/null || true
sed -i 's/\/\/ mi,mdss-dsi-pan-enable-smart-fps/mi,mdss-dsi-pan-enable-smart-fps/g' ${dts_source}/dsi-panel-l11r* 2>/dev/null || true
sed -i 's/\/\/ mi,mdss-dsi-smart-fps-max_framerate/mi,mdss-dsi-smart-fps-max_framerate/g' ${dts_source}/dsi-panel-l11r* 2>/dev/null || true
sed -i 's/qcom,mdss-dsi-qsync-min-refresh-rate/\/\/qcom,mdss-dsi-qsync-min-refresh-rate/g' ${dts_source}/dsi-panel-l11r* 2>/dev/null || true

make $MAKE_ARGS $DEFCONFIG_MIUI
apply_ksu_susfs_config

scripts/config --file out/.config \
    --set-str STATIC_USERMODEHELPER_PATH /system/bin/micd \
    -e PERF_CRITICAL_RT_TASK \
    -e SF_BINDER \
    -e OVERLAY_FS \
    -d DEBUG_FS \
    -e MIGT \
    -e MIGT_ENERGY_MODEL \
    -e MIHW \
    -e PACKAGE_RUNTIME_INFO \
    -e BINDER_OPT \
    -e KPERFEVENTS \
    -e MILLET \
    -e PERF_HUMANTASK \
    -d LTO_CLANG \
    -d LOCALVERSION_AUTO \
    -e XIAOMI_MIUI \
    -d MI_MEMORY_SYSFS \
    -e TASK_DELAY_ACCT \
    -e MIUI_ZRAM_MEMORY_TRACKING \
    -d CONFIG_MODULE_SIG_SHA512 \
    -d CONFIG_MODULE_SIG_HASH \
    -e MI_FRAGMENTION \
    -e PERF_HELPER \
    -e BOOTUP_RECLAIM \
    -e MI_RECLAIM \
    -e RTMM

make $MAKE_ARGS -j$(nproc)

if [ ! -f "out/arch/arm64/boot/Image" ]; then
    echo "Lỗi: Không tìm thấy out/arch/arm64/boot/Image cho MIUI!"
    rm -rf ${dts_source} && mv .dts.bak ${dts_source}
    exit 1
fi

find out/arch/arm64/boot/dts/vendor/qcom/ -name '*kona*.dtb' -exec cat {} + > out/arch/arm64/boot/dtb 2>/dev/null || \
find out/arch/arm64/boot/dts/ -name '*.dtb' -exec cat {} + > out/arch/arm64/boot/dtb

# Phục hồi DTS gốc
rm -rf ${dts_source}
mv .dts.bak ${dts_source}

patch_kpm_image

rm -rf anykernel/kernels/ && mkdir -p anykernel/kernels/
cp out/arch/arm64/boot/Image anykernel/kernels/
cp out/arch/arm64/boot/dtb anykernel/kernels/

# Khôi phục chuỗi localversion defconfig
[ -f "arch/arm64/configs/${DEFCONFIG_AOSP}" ] && sed -i "s/${local_version_date_str}/${local_version_str}/g" "arch/arm64/configs/${DEFCONFIG_AOSP}"
[ -f "arch/arm64/configs/${DEFCONFIG_MIUI}" ] && sed -i "s/${local_version_date_str}/${local_version_str}/g" "arch/arm64/configs/${DEFCONFIG_MIUI}"

cd anykernel
ZIP_MIUI="Kernel_MIUI_munch_${KSU_ZIP_STR}_$(date +'%Y%m%d_%H%M%S')_${GIT_COMMIT_ID}.zip"
zip -r9 "$ZIP_MIUI" ./* -x .git .gitignore out/ ./*.zip
mv "$ZIP_MIUI" ../
cd ..

echo "=========================================="
echo ">> Hoàn tất build kernel cho munch:"
echo "   - File AOSP : [./$ZIP_AOSP]"
echo "   - File MIUI : [./$ZIP_MIUI]"
echo "=========================================="
ce}/dsi-panel-j11-38-08-0a-fhd-cmd.dtsi
sed -i 's/\/\/39 01 00 00 11 00 03 51 03 FF/39 01 00 00 11 00 03 51 03 FF/g' ${dts_source}/dsi-panel-j2-p2-1-38-0c-0a-dsc-cmd.dtsi


make $MAKE_ARGS ${TARGET_DEVICE}_defconfig

if [ $KSU_ENABLE -eq 1 ]; then
    scripts/config --file out/.config \
    -e KSU \
    -e KSU_SUSFS \
    -e KSU_SUSFS_SUS_PATH \
    -e KSU_SUSFS_SUS_MOUNT \
    -e KSU_SUSFS_SUS_KSTAT \
    -e KSU_SUSFS_SPOOF_UNAME \
    -e KSU_SUSFS_ENABLE_LOG \
    -e KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS \
    -e KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG \
    -e KSU_SUSFS_OPEN_REDIRECT \
    -e KSU_SUSFS_SUS_MAP \
    -e THREAD_INFO_IN_TASK \
    -e KPM
else
    scripts/config --file out/.config -d KSU
fi


scripts/config --file out/.config \
    --set-str STATIC_USERMODEHELPER_PATH /system/bin/micd \
    -e PERF_CRITICAL_RT_TASK	\
    -e SF_BINDER		\
    -e OVERLAY_FS		\
    -d DEBUG_FS \
    -e MIGT \
    -e MIGT_ENERGY_MODEL \
    -e MIHW \
    -e PACKAGE_RUNTIME_INFO \
    -e BINDER_OPT \
    -e KPERFEVENTS \
    -e MILLET \
    -e PERF_HUMANTASK \
    -d LTO_CLANG \
    -d LOCALVERSION_AUTO \
    -e SF_BINDER \
    -e XIAOMI_MIUI \
    -d MI_MEMORY_SYSFS \
    -e TASK_DELAY_ACCT \
    -e MIUI_ZRAM_MEMORY_TRACKING \
    -d CONFIG_MODULE_SIG_SHA512 \
    -d CONFIG_MODULE_SIG_HASH \
    -e MI_FRAGMENTION \
    -e PERF_HELPER \
    -e BOOTUP_RECLAIM \
    -e MI_RECLAIM \
    -e RTMM \

make $MAKE_ARGS -j$(nproc)



if [ -f "out/arch/arm64/boot/Image" ]; then
    echo "The file [out/arch/arm64/boot/Image] exists. MIUI Build successfully."
else
    echo "The file [out/arch/arm64/boot/Image] does not exist. Seems MIUI build failed."
    exit 1
fi

echo "Generating [out/arch/arm64/boot/dtb]......"
find out/arch/arm64/boot/dts -name '*.dtb' -exec cat {} + >out/arch/arm64/boot/dtb


# Restore modified dts
rm -rf ${dts_source}
mv .dts.bak ${dts_source}

rm -rf anykernel/kernels/
mkdir -p anykernel/kernels/

# Patch for SukiSU KPM support. 
if [ $KSU_ENABLE -eq 1 ]; then
    cd out/arch/arm64/boot/
    # wget https://github.com/SukiSU-Ultra/SukiSU_KernelPatch_patch/releases/download/0.12.2/patch_linux
    wget https://github.com/SukiSU-Ultra/SukiSU_KernelPatch_patch/releases/latest/download/patch_linux
    chmod +x patch_linux
    ./patch_linux
    rm Image
    mv oImage Image
    cd -
fi

cp out/arch/arm64/boot/Image anykernel/kernels/
cp out/arch/arm64/boot/dtb anykernel/kernels/

echo "Build for MIUI finished."

# Restore local version string
sed -i "s/${local_version_date_str}/${local_version_str}/g" arch/arm64/configs/${TARGET_DEVICE}_defconfig

# ------------- End of Building for MIUI -------------
#  If you don't need MIUI you can comment out the above block [Building for MIUI]


cd anykernel 

ZIP_FILENAME=Kernel_MIUI_${TARGET_DEVICE}_${KSU_ZIP_STR}_$(date +'%Y%m%d_%H%M%S')_anykernel3_${GIT_COMMIT_ID}.zip

zip -r9 $ZIP_FILENAME ./* -x .git .gitignore out/ ./*.zip

mv $ZIP_FILENAME ../

cd ..

echo "Done. The flashable zip is: [./$ZIP_FILENAME]"
