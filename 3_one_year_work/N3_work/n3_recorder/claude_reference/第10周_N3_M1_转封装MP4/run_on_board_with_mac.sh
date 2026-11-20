#!/bin/sh
# 来源：n0 第 02 周快照的 run_on_board_with_mac.sh。编译 → adb push → adb shell 运行，全程走 USB。
# 用法：./run_on_board_with_mac.sh m1 [输入文件或 rtsp://URL] [输出 mp4]
# 板子 /usr/lib 的 FFmpeg 就是 4.4.1，不需要 LD_LIBRARY_PATH。先 . /etc/profile：拿到 TZ=CST-8，文件名、索引用本地时间
BIN_NAME=n3_recorder
BUILD_DIR=${BUILD_DIR:-build}
REMOTE_DIR=/tmp/CLion/run

cd "$(dirname "$0")" || exit 1
ADB=${ADB:-adb}
command -v "$ADB" >/dev/null 2>&1 || { echo "找不到 adb" >&2; exit 1; }
[ -n "$ADB_SERIAL" ] && ADB="$ADB -s $ADB_SERIAL"

CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

$ADB start-server >/dev/null 2>&1
$ADB get-state >/dev/null 2>&1 || { echo "adb 没找到板子，先 adb devices 看看" >&2; exit 1; }

$ADB shell "mkdir -p $REMOTE_DIR" < /dev/null || exit 1
$ADB push "$BUILD_DIR/$BIN_NAME" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
echo "=== 板子上运行 $BIN_NAME $* ==="
exec $ADB shell ". /etc/profile; $REMOTE_DIR/$BIN_NAME $*" < /dev/null
