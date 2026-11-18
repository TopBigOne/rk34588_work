#!/bin/sh
# 编译 day8_qt_player，通过 USB（adb）传到 RK3588 开发板上运行。不走网络，不需要板子 IP。
# 用法：
#   ./run_on_board_with_mac.sh [程序参数...]
# 例：
#   ./run_on_board_with_mac.sh /userdata/day8_media/sync_test_1080p.mp4             播放器（画面 + 声音）
#   ./run_on_board_with_mac.sh /userdata/day8_media/sync_test_1080p.mp4 --no-audio  M2：只播画面
#   ./run_on_board_with_mac.sh m1 /userdata/day8_media/sync_test_1080p.mp4 /userdata/day8_media/m1.nv12
#   ./run_on_board_with_mac.sh m3 /userdata/day8_media/sync_test_1080p.mp4 /userdata/day8_media/m3.pcm
# 不写参数时默认播放 /userdata/day8_media/sync_test_1080p.mp4。
# 测试视频不在板子上时，自动从 test_media/ 推上去（先运行 ./make_test_media.sh 生成）。
# 可选环境变量：
#   ADB_SERIAL=xxxx               同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）
#   PLAYER_ALSA_DEVICE=plughw:1,0 指定声卡，原样传给板子上的程序
#   PLAYER_SCREENSHOT_PATH=/tmp/day8_player.png  调试用：播放器启动 3 秒后把窗口截图存到板子上的这个路径

BIN_NAME=day8_qt_player
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run
REMOTE_MEDIA_DIR=/userdata/day8_media
LOCAL_TEST_MEDIA=test_media/sync_test_1080p.mp4

# 连上板子桌面（weston）要用的 3 个环境变量（2026-11-17 day7 实测：weston 的 socket 是 /var/run/wayland-0）：
#   XDG_RUNTIME_DIR   Wayland 连接文件（socket）所在的目录
#   WAYLAND_DISPLAY   socket 的文件名
#   QT_QPA_PLATFORM   Qt 用哪个显示插件，wayland → /usr/lib/qt/plugins/platforms/libqwayland-egl.so
# LD_LIBRARY_PATH：优先用自己编译的新版 MPP（程序里也写了 rpath，这里双保险）
BOARD_ENV="XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland LD_LIBRARY_PATH=/userdata/mpp_build/lib"
if [ -n "$PLAYER_ALSA_DEVICE" ]; then
    BOARD_ENV="$BOARD_ENV PLAYER_ALSA_DEVICE=$PLAYER_ALSA_DEVICE"
fi
if [ -n "$PLAYER_SCREENSHOT_PATH" ]; then
    BOARD_ENV="$BOARD_ENV PLAYER_SCREENSHOT_PATH=$PLAYER_SCREENSHOT_PATH"
fi

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
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_MAKE_PROGRAM=/Applications/CLion.app/Contents/bin/ninja/mac/aarch64/ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake || exit 1
fi
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

# 第 2 步 确认板子连上了
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 第 3 步 没给参数时用默认的测试视频
if [ $# -eq 0 ]; then
    set -- "$REMOTE_MEDIA_DIR/sync_test_1080p.mp4"
fi

# 第 4 步 测试视频不在板子上就推上去。所有 adb shell 都加 < /dev/null：不让 adb 读终端输入
#   注意：这块板子的 adbd 不把板子上命令的退出码传回来（2026-11-18 实测：adb shell 'test -f /不存在' 在 Mac 上 $? 也是 0），
#   所以不能写 if adb shell "test -f ..."，要让板子 echo 出结果，在 Mac 上比较输出的文字
$ADB shell "mkdir -p $REMOTE_DIR $REMOTE_MEDIA_DIR" < /dev/null || exit 1
MEDIA_STATE=$($ADB shell "test -f $REMOTE_MEDIA_DIR/sync_test_1080p.mp4 && echo yes || echo no" < /dev/null | tr -d '\r')
if [ "$MEDIA_STATE" != "yes" ]; then
    if [ ! -f "$LOCAL_TEST_MEDIA" ]; then
        echo "板子上和本地都没有测试视频，先运行 ./make_test_media.sh" >&2
        exit 1
    fi
    echo "=== 推送测试视频到 $REMOTE_MEDIA_DIR ==="
    $ADB push "$LOCAL_TEST_MEDIA" "$REMOTE_MEDIA_DIR/" || exit 1
fi

# 第 5 步 播放器模式要有 weston（m1、m3 不需要）
if [ "$1" != "m1" ] && [ "$1" != "m3" ]; then
    # pidof 找到进程时输出 pid，找不到时什么都不输出（退出码传不回来，同第 4 步），所以看输出是不是空的
    WESTON_PID=$($ADB shell "pidof weston" < /dev/null | tr -d '\r')
    if [ -z "$WESTON_PID" ]; then
        echo "板子上的 weston（桌面）没在运行，窗口没地方显示。先在板子上执行 /etc/init.d/S49weston start 或重启板子" >&2
        exit 1
    fi
fi

# 第 6 步 推程序、运行
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
echo "=== 通过 USB 在板子上运行 $REMOTE_DIR/$BIN_NAME $* ==="
exec $ADB shell "$BOARD_ENV $REMOTE_DIR/$BIN_NAME $*" < /dev/null
