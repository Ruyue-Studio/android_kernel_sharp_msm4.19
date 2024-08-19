#!/bin/bash

# 设置编译架构
export ARCH=arm64
export SUBARCH=arm64

# 配置编译器和工具链路径
CLANG_PATH=$(pwd)/prebuilts/clang/host/linux-x86/clang-r377782d/bin/clang
CROSS_COMPILE_PATH=$(pwd)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-

# 生成配置
make ARCH=arm64 \
    CC=$CLANG_PATH CLANG_TRIPLE=aarch64-linux-gnu- \
    CROSS_COMPILE=$CROSS_COMPILE_PATH \
    banagher_defconfig

# 开始编译，指定编译标志和工具链
make -j20 ARCH=arm64 \
    CC=$CLANG_PATH CLANG_TRIPLE=aarch64-linux-gnu- \
    CROSS_COMPILE=$CROSS_COMPILE_PATH \
    CROSS_COMPILE_ARM32=$CROSS_COMPILE_ARM32_PATH \
    CFLAGS="-Wno-format -Wno-error"

