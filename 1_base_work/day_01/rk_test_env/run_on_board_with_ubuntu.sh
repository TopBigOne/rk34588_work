#!/bin/sh
# 把编译好的程序上传到 RK3588 开发板并运行，输出显示在当前终端（CLion 的 Run 窗口）。—— Ubuntu 版
# 用法：./run_on_board_with_ubuntu.sh [程序参数...]
# 板子 IP：默认通过 adb 自动获取（调用 ../../common_shell/get_board_ip.sh，OTG 线要插在这台电脑上）；
#          也可以手动指定：BOARD_IP=192.168.1.xx ./run_on_board_with_ubuntu.sh
# 和 Mac 版（run_on_board_with_mac.sh）的区别：只有 CMAKE 的路径不一样

BIN_NAME=rk_test_env
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

# 没有手动指定 BOARD_IP 时，自动获取
if [ -z "$BOARD_IP" ]; then
    BOARD_IP=$(../../common_shell/get_board_ip.sh) || exit 1
    echo "=== 自动获取到板子 IP：$BOARD_IP ==="
fi

# 先编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588-Debug 配置生成的那个）
CMAKE=/home/dev/Documents/IDE/clion-2026.1.2/bin/cmake/linux/x64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 $LOCAL_BIN，请先编译（Ctrl+F9）" >&2
    exit 1
fi

ssh root@"$BOARD_IP" "mkdir -p $REMOTE_DIR" || exit 1
scp -q "$LOCAL_BIN" root@"$BOARD_IP":"$REMOTE_DIR/" || exit 1
echo "=== 在 $BOARD_IP 上运行 $REMOTE_DIR/$BIN_NAME ==="
exec ssh root@"$BOARD_IP" "$REMOTE_DIR/$BIN_NAME $*"
