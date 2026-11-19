#!/bin/bash
# 用正点原子工具链交叉编译 FFmpeg 6.0，给板子上的 MppPlayer 用。
#
# 为什么要自己编：板子 /usr/lib 里的 ffmpeg 是 4.4.1，MppPlayer 是从 FFmpeg 6.0 的 ffplay 移植来的，
# 用到 AVChannelLayout、av_fifo_alloc2、swr_alloc_set_opts2 这些 6.0 才有的接口。
#
# 做的事：
#   1. 从本机 FFmpeg-6.0 仓库 git archive 出一份干净源码（本机源码目录里有 macOS 编译留下的 config.h，
#      FFmpeg 规定源码目录里有 config.h 时不能在别的目录编译，所以不直接用它）
#   2. 在 OrbStack 的 rk-dev（x86_64 Ubuntu）里 configure + make + make install
#      （configure 要试编译几千次，在 rk-dev 里直接跑比经过 Mac 的转发脚本快得多）
#   3. 装到 FFMPEG6_INSTALL_DIR（Mac 和 rk-dev 都能用同一路径访问），CMake 从这里找头文件和库
#
# 板子上：run 脚本把 FFMPEG6_INSTALL_DIR/lib 推到 /userdata/ffmpeg6/lib，
#         程序用 rpath 写死了这个目录，不会加载到 /usr/lib 里的 4.4.1
#
# 用法：./build_ffmpeg6_for_board.sh        （大约 10～15 分钟）

set -e

FFMPEG_SOURCE_REPO=/Users/dev/Documents/AV/FFmpeg-6.0
FFMPEG6_SOURCE_DIR=/Users/dev/rk-toolchain/ffmpeg6-src
FFMPEG6_INSTALL_DIR=/Users/dev/rk-toolchain/ffmpeg6-rk3588
RK_TOOLCHAIN_ROOT=/Users/dev/rk-toolchain/atk-dlrk3588-toolchain
RK_SYSROOT=$RK_TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/sysroot

# 第 1 步 导出干净源码
if [ ! -f "$FFMPEG6_SOURCE_DIR/configure" ]; then
    echo "==> 导出 FFmpeg 源码到 $FFMPEG6_SOURCE_DIR"
    mkdir -p "$FFMPEG6_SOURCE_DIR"
    (cd "$FFMPEG_SOURCE_REPO" && git archive HEAD) | tar -x -C "$FFMPEG6_SOURCE_DIR"
fi

# 第 2 步 在 rk-dev 里编译
# 各个 configure 参数：
#   --enable-cross-compile --arch --target-os：交叉编译，目标是 aarch64 Linux
#   --cross-prefix：编译器、ar、strip 等工具名的前缀（工具链 bin 目录已加进 PATH）
#   --sysroot：板子根文件系统的副本，头文件和 libz.so 从这里找
#   --enable-shared --disable-static：编成 .so，板子上和程序分开放
#   --enable-gpl：libpostproc 要求 GPL（SimpleFFPlay 的 CMake 链接了 postproc）
#   --disable-autodetect：不自动探测 sysroot 里的第三方库（SDL、pulse、vaapi……），
#                         只打开明确写出来的 zlib，免得多依赖板子上可能没有的库；rk-dev 里也没装 pkg-config
#   --enable-zlib：mov/mp4 拆封装遇到压缩的 moov 头要用
#   --disable-programs --disable-doc：不编 ffmpeg/ffprobe/ffplay 命令行工具和文档
#   --enable-debug=3 --disable-stripping：保留调试符号，gdb 能进到 FFmpeg 函数里
echo "==> 在 rk-dev 里 configure + make"
orb -m rk-dev bash -c "
set -e
export PATH=$RK_TOOLCHAIN_ROOT/bin:\$PATH
mkdir -p $FFMPEG6_SOURCE_DIR/build-aarch64
cd $FFMPEG6_SOURCE_DIR/build-aarch64
../configure \
    --prefix=$FFMPEG6_INSTALL_DIR \
    --enable-cross-compile \
    --arch=aarch64 \
    --target-os=linux \
    --cross-prefix=aarch64-buildroot-linux-gnu- \
    --sysroot=$RK_SYSROOT \
    --enable-shared \
    --disable-static \
    --enable-gpl \
    --disable-autodetect \
    --enable-zlib \
    --disable-programs \
    --disable-doc \
    --enable-debug=3 \
    --disable-stripping
make -j\$(nproc)
make install
"

echo "==> 完成，库在 $FFMPEG6_INSTALL_DIR/lib"
ls "$FFMPEG6_INSTALL_DIR/lib"
