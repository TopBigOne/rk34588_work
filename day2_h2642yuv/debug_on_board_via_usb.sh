#!/bin/sh
# 通过 USB（adb）在 RK3588 板子上调试 day2_h2642yuv，不需要板子 IP、不走网络。
#
# 原理：
#   CLion 的 GDB ──连 localhost:1234──→ adb forward ──USB──→ 板子上的 gdbserver :1234 ──→ day2_h2642yuv
#
# 用法（两步）：
#   1. 运行本脚本（CLion 里的 Shell Script 配置，或者终端里 ./debug_on_board_via_usb.sh [程序参数...]）
#      看到 "Listening on port 1234" 就说明 gdbserver 在等 GDB 连上来了。脚本会一直停在这里，
#      程序运行时打印的内容也会显示在这个窗口里
#   2. 在 CLion 里选 "Remote Debug" 配置（target remote: localhost:1234），点 🐞 调试
#   调试结束（程序跑完或者在 CLion 里点停止）后，gdbserver 退出，本脚本也跟着结束
#
# 程序参数会原样传给 day2_h2642yuv，例如：
#   ./debug_on_board_via_usb.sh -i /userdata/av/aaa.264 -o /userdata/av/out.nv12 -n 10
#
# 可选环境变量：
#   ADB=/path/to/adb    不用 PATH 里的 adb 时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）
#   GDB_PORT=1234       换一个端口

BIN_NAME=day2_h2642yuv
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/debug          # 和运行脚本的 /tmp/CLion/run 分开
GDB_PORT=${GDB_PORT:-1234}

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

# adb 直接用 PATH 里的（Mac 上已经加进环境变量；Ubuntu：sudo apt install adb）
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里（Ubuntu: sudo apt install adb），或者设置 ADB=adb的路径" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 1. 编译（用 CLion 自带的 cmake，编译目录是 RK3588-Debug 配置生成的那个）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1
if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 ${LOCAL_BIN}，请先编译（Cmd+F9）" >&2
    exit 1
fi

# 2. 确认板子连上了
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 3. 推程序；顺手结束上一次没退出的 gdbserver（否则端口被占用）
$ADB shell "mkdir -p $REMOTE_DIR; pkill gdbserver" >/dev/null 2>&1
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" || exit 1

# 4. 端口转发：Mac 的 localhost:GDB_PORT → 板子的 GDB_PORT（走 USB）
$ADB forward "tcp:$GDB_PORT" "tcp:$GDB_PORT" >/dev/null || exit 1

# 5. 在板子上启动 gdbserver，停在程序第一条指令，等 GDB 连上来
#    程序带了 rpath（见 CMakeLists.txt），会自动加载 /userdata/mpp_build/lib 里的 MPP 库，不用设 LD_LIBRARY_PATH
echo "=== 板子上启动 gdbserver :$GDB_PORT $REMOTE_DIR/$BIN_NAME $* ==="
echo "=== 现在去 CLion 选 Remote Debug 配置（localhost:${GDB_PORT}），点调试 ==="
exec $ADB shell "gdbserver :$GDB_PORT $REMOTE_DIR/$BIN_NAME $*"
