#!/bin/sh
# 把编译好的 Qt 程序通过 USB（adb）传到 RK3588 开发板，在板子的 weston 桌面上开窗口运行。
# 程序的输出显示在当前终端（CLion 的 Run 窗口），窗口显示在板子接的屏幕上。
# 不走网络，不需要板子 IP，OTG 线（TypeC0 口）插在这台电脑上就行。
# 用法：./run_on_board_with_mac.sh [程序参数...]，按 Ctrl+C 或在板子上关窗口结束
# 可选环境变量：
#   ADB=/path/to/adb    不用 PATH 里的 adb 时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=day7_qt_env
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run

# 连上板子桌面（weston）要用的 3 个环境变量，不设的话窗口出不来：
#   XDG_RUNTIME_DIR   Wayland 连接文件（socket）所在的目录。2026-11-17 实测：weston 进程的环境变量里是 /var/run，
#                     socket 是 /var/run/wayland-0
#   WAYLAND_DISPLAY   socket 的文件名
#   QT_QPA_PLATFORM   Qt 用哪个显示插件，wayland → 板子上的 /usr/lib/qt/plugins/platforms/libqwayland-egl.so
BOARD_DISPLAY_ENV="XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland"

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

# adb 直接用 PATH 里的（Mac 上已经加进环境变量）
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里，或者设置 ADB=adb的路径" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 先编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588 配置生成的那个）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 ${LOCAL_BIN}，请先编译（Cmd+F9）" >&2
    exit 1
fi

# 确认板子连上了
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 确认板子上的 weston 在运行：没有桌面的话，Qt 连不上 Wayland，会直接报错退出
if ! $ADB shell "pidof weston" < /dev/null >/dev/null 2>&1; then
    echo "板子上的 weston（桌面）没在运行，窗口没地方显示。先在板子上执行 /etc/init.d/S49weston start 或重启板子" >&2
    exit 1
fi

# 所有 adb shell 都加 < /dev/null：不让 adb 读终端输入（原因见 day5_transcoder/run_on_board_with_mac.sh）
$ADB shell "mkdir -p $REMOTE_DIR" < /dev/null || exit 1
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
echo "=== 通过 USB 在板子的 weston 桌面上运行 $REMOTE_DIR/$BIN_NAME $* ==="
exec $ADB shell "$BOARD_DISPLAY_ENV $REMOTE_DIR/$BIN_NAME $*" < /dev/null
