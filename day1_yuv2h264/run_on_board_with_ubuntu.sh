#!/bin/sh
# 把编译好的程序上传到 RK3588 开发板并运行，输出显示在当前终端（CLion 的 Run 窗口）。—— Ubuntu 版
# 用法：./run_on_board_with_ubuntu.sh [程序参数...]
#
# 传输方式开关 USE_USB（改下面的默认值，或者运行时临时指定）：
#   1：走 USB（adb push / adb shell），不需要板子 IP，OTG 线（TypeC0 口）插在这台电脑上就行
#   0：走网络（scp / ssh），需要板子 IP
#   临时指定：USE_USB=0 ./run_on_board_with_ubuntu.sh
#
# 走 scp 时的板子 IP：默认通过 adb 自动获取（调用 ../common_shell/get_board_ip.sh）；
#          也可以手动指定：BOARD_IP=192.168.1.xx USE_USB=0 ./run_on_board_with_ubuntu.sh
# 走 USB 时的可选环境变量：
#   ADB=/path/to/adb    adb 不在 PATH 里时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）
# 和 Mac 版（run_on_board_with_mac.sh）的区别：CMAKE 的路径不一样；Mac 版只走 USB

USE_USB=${USE_USB:-1}

BIN_NAME=day1_yuv2h264
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run
# 板子 /usr/lib 里出厂的 librockchip_mpp 太旧（缺 mpp_buffer_sync_begin_f 等函数），
# 运行时优先加载自己编译的 MPP 库（rk_code/external/mpp 编出来、放在板子上的那份）
BOARD_MPP_LIB=/userdata/mpp_build/lib

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

if [ "$USE_USB" != "0" ] && [ "$USE_USB" != "1" ]; then
    echo "USE_USB 只能是 0（scp）或 1（USB），现在是：$USE_USB" >&2
    exit 1
fi

if [ "$USE_USB" = "1" ]; then
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
elif [ -z "$BOARD_IP" ]; then
    # 走 scp 且没有手动指定 BOARD_IP 时，自动获取
    BOARD_IP=$(../common_shell/get_board_ip.sh) || exit 1
    echo "=== 自动获取到板子 IP：$BOARD_IP ==="
fi

# 先编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588-Debug 配置生成的那个）
CMAKE=/home/dev/Documents/IDE/clion-2026.1.2/bin/cmake/linux/x64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 $LOCAL_BIN，请先编译（Ctrl+F9）" >&2
    exit 1
fi

if [ "$USE_USB" = "1" ]; then
    # ---------- USB ----------
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
else
    # ---------- scp ----------
    ssh root@"$BOARD_IP" "mkdir -p $REMOTE_DIR" || exit 1
    scp -q "$LOCAL_BIN" root@"$BOARD_IP":"$REMOTE_DIR/" || exit 1
    echo "=== 在 $BOARD_IP 上运行 $REMOTE_DIR/$BIN_NAME（MPP 库：$BOARD_MPP_LIB） ==="
    exec ssh root@"$BOARD_IP" "LD_LIBRARY_PATH=$BOARD_MPP_LIB $REMOTE_DIR/$BIN_NAME $*"
fi
