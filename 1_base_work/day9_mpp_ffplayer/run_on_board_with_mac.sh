#!/bin/sh
# 编译 MppPlayer，通过 USB（adb）传到 RK3588 开发板上运行。不走网络，不需要板子 IP。
# 用法：
#   ./run_on_board_with_mac.sh [ffplay 参数...] [视频路径]
# 例：
#   ./run_on_board_with_mac.sh                                                  默认：-autoexit 播 sync_test_1080p.mp4
#   ./run_on_board_with_mac.sh -an /userdata/day8_media/sync_test_1080p.mp4      不出声（-an = 不打开音频）
#   ./run_on_board_with_mac.sh -fs /userdata/day8_media/yequ_640x480.mp4         全屏
#   ./run_on_board_with_mac.sh -nomppdec -an /userdata/day8_media/sync_test_1080p.mp4   软解（对比用）
# 参数和 ffplay 一样（-x/-y 窗口大小、-fs 全屏、-an/-vn、-ss 起始时间、-autoexit、-loop ...），另加 -nomppdec。
# 测试视频用 day8 推到板子上的 /userdata/day8_media/（不在时到 day8_qt_player 目录运行 make_test_media.sh 再推）。
# 可选环境变量：
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=MppPlayer
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run
DEFAULT_MEDIA_PATH=/userdata/day8_media/sync_test_1080p.mp4

# 板子上运行要的环境变量：
#   XDG_RUNTIME_DIR、WAYLAND_DISPLAY  连 weston（板子桌面）的 socket：/var/run/wayland-0（2026-11-17 day7 实测）
#   SDL_VIDEODRIVER=wayland           SDL 用 Wayland 显示（板子的 SDL2 只编了 wayland、dummy、offscreen 三种）
#   LD_LIBRARY_PATH                   程序里已经用 rpath 写了这两个目录，这里双保险
BOARD_ENV="XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 SDL_VIDEODRIVER=wayland LD_LIBRARY_PATH=/userdata/ffmpeg6/lib:/userdata/mpp_build/lib"

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

ADB=adb
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 第 1 步 编译（用 CLion 自带的 cmake；编译目录还没生成时先配置一次）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake || exit 1
fi
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" -j8 || exit 1

# 第 2 步 确认板子连上了
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 第 3 步 FFmpeg 6.0 的库（板子上已经是最新就跳过）
./push_ffmpeg6_to_board.sh || exit 1

# 第 4 步 没给参数时：播完自动退出，播默认测试视频
if [ $# -eq 0 ]; then
    set -- -autoexit "$DEFAULT_MEDIA_PATH"
fi

# 第 5 步 要有 weston，窗口才有地方显示。
# pidof 找到进程时输出 pid，找不到时什么都不输出（板子 adbd 不回传退出码），所以看输出是不是空的
WESTON_PID=$($ADB shell "pidof weston" < /dev/null | tr -d '\r')
if [ -z "$WESTON_PID" ]; then
    echo "板子上的 weston（桌面）没在运行，窗口没地方显示。先在板子上执行 /etc/init.d/S49weston start 或重启板子" >&2
    exit 1
fi

# 第 6 步 推程序、运行。所有 adb shell 都加 < /dev/null：不让 adb 读终端输入
$ADB shell "mkdir -p $REMOTE_DIR" < /dev/null || exit 1
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
echo "=== 通过 USB 在板子上运行 $REMOTE_DIR/$BIN_NAME $* ==="
exec $ADB shell "$BOARD_ENV $REMOTE_DIR/$BIN_NAME $*" < /dev/null
