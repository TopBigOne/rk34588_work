#!/bin/sh
# 把编译好的程序通过 USB（adb）传到 RK3588 开发板并运行，输出显示在当前终端（CLion 的 Run 窗口）。
# 不走网络，不需要板子 IP，OTG 线（TypeC0 口）插在这台电脑上就行。
# 用法：./run_on_board_with_mac.sh [程序参数...]
# 可选环境变量：
#   ADB=/path/to/adb    adb 不在 PATH 里时指定（Mac 上默认会去找 ~/Documents/Android_Env/sdk/platform-tools/adb）
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=day1_yuv2h264
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run
# 板子 /usr/lib 里出厂的 librockchip_mpp 太旧（缺 mpp_buffer_sync_begin_f 等函数），
# 运行时优先加载自己编译的 MPP 库（rk_code/external/mpp 编出来、放在板子上的那份）
BOARD_MPP_LIB=/userdata/mpp_build/lib

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

# 找 adb（和 run_move_file_to_board_via_usb.sh 一样）
if [ -z "$ADB" ]; then
    if command -v adb >/dev/null 2>&1; then
        ADB=adb
    elif [ -x "$HOME/Documents/Android_Env/sdk/platform-tools/adb" ]; then
        ADB="$HOME/Documents/Android_Env/sdk/platform-tools/adb"
    else
        echo "找不到 adb（Ubuntu: sudo apt install adb；或设置 ADB=adb的路径）" >&2
        exit 1
    fi
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 先编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588-Debug 配置生成的那个）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 $LOCAL_BIN，请先编译（Cmd+F9）" >&2
    exit 1
fi

# 确认板子连上了
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

$ADB shell "mkdir -p $REMOTE_DIR" || exit 1
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" || exit 1
echo "=== 通过 USB 在板子上运行 $REMOTE_DIR/$BIN_NAME $*（MPP 库：$BOARD_MPP_LIB） ==="
exec $ADB shell "LD_LIBRARY_PATH=$BOARD_MPP_LIB $REMOTE_DIR/$BIN_NAME $*"
