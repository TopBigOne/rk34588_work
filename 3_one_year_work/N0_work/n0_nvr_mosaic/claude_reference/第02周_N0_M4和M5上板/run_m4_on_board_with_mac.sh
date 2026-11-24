#!/bin/sh
# M4 / M5 上屏：编译 → adb push → 板子上建 FIFO → 后台起 gst-launch-1.0 从 FIFO 读码流、硬解、显示到屏幕 → 前台跑程序往 FIFO 里写。
# 全程走 USB（adb），不用板子 IP。
# 用法：./run_m4_on_board_with_mac.sh [m4 | m5screen]   不写默认 m4
# 可选环境变量：
#   BUILD_DIR=build      编译目录（默认 build）
#   ADB_SERIAL=xxxx      同时连了多台 adb 设备时，指定板子的序列号
#   GST_SINK=waylandsink gst 的显示元素（默认 waylandsink）；故意写错可以验证“gst 起不来时程序会被停掉”
#
# 状态：2026-11-20 写好，还没上板（板子不在身边）

BIN_NAME=n0_nvr_mosaic
BUILD_DIR=${BUILD_DIR:-build}
MILESTONE=${1:-m4}
REMOTE_DIR=/tmp/CLion/run
BOARD_MPP_LIB=/userdata/mpp_build/lib
FIFO_PATH=/tmp/n0_mosaic.fifo   # 必须和 src/app/app_config.h 里的 kM4FifoPath 一样
GST_LOG=/tmp/n0_gst.log
GST_SINK=${GST_SINK:-waylandsink}

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

$ADB shell "mkdir -p $REMOTE_DIR" < /dev/null || exit 1
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1

# 板子上执行的脚本（一次 adb shell 跑完，两个进程都在同一个 shell 里，结束时一起收拾）：
#   第 1 步 重新建 FIFO（mkfifo：建一个命名管道文件，一头写、一头读，数据不落盘）
#   第 2 步 后台起 gst：filesrc 读 FIFO → h265parse 切出一帧帧 → mppvideodec 用 MPP 硬解 → 显示元素画到屏幕
#           XDG_RUNTIME_DIR、WAYLAND_DISPLAY：weston 的 socket 在 /var/run/wayland-0（实测）
#   第 3 步 后台起程序往 FIFO 里写；程序打开 FIFO 写端时，要等 gst 那边把读端打开才会往下走
#   第 4 步 盯着两个进程：gst 先退出而程序还在（比如 gst 起不来，程序会一直卡在打开 FIFO 那一步），就把程序杀掉
#   第 5 步 等两个进程都结束，删掉 FIFO，用程序的返回码作为整个脚本的返回码
BOARD_SCRIPT="
rm -f $FIFO_PATH; mkfifo $FIFO_PATH || exit 1
XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 gst-launch-1.0 filesrc location=$FIFO_PATH ! h265parse ! mppvideodec ! $GST_SINK > $GST_LOG 2>&1 &
GST_PID=\$!
LD_LIBRARY_PATH=$BOARD_MPP_LIB $REMOTE_DIR/$BIN_NAME $MILESTONE &
APP_PID=\$!
while kill -0 \$APP_PID 2>/dev/null; do
    if ! kill -0 \$GST_PID 2>/dev/null; then
        echo '[run_m4] gst 已经退出，程序还在，杀掉程序。gst 日志：'; cat $GST_LOG
        kill \$APP_PID; break
    fi
    sleep 1
done
wait \$APP_PID; APP_RET=\$?
wait \$GST_PID
rm -f $FIFO_PATH
echo \"[run_m4] 程序返回 \$APP_RET；gst 日志在 $GST_LOG\"
exit \$APP_RET
"
echo "=== 通过 USB 在板子上运行 $BIN_NAME $MILESTONE，输出经 FIFO 交给 gst 显示（$GST_SINK） ==="
exec $ADB shell "$BOARD_SCRIPT" < /dev/null
