#!/bin/sh
# 把编译好的程序上传到 RK3588 开发板并运行，输出显示在当前终端（CLion 的 Run 窗口）。
# 用法：./run_on_board_with_mac.sh [程序参数...]
# 板子 IP 变了的话，改下面的 BOARD_IP，或者运行前设置环境变量：BOARD_IP=192.168.1.xx ./run_on_board_with_mac.sh

BOARD_IP=${BOARD_IP:-192.168.1.63}
BIN_NAME=rk_test_env
BUILD_DIR=cmake-build-rk3588-debug
REMOTE_DIR=/tmp/CLion/run

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

# 先编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588-Debug 配置生成的那个）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

if [ ! -f "$LOCAL_BIN" ]; then
    echo "找不到 $LOCAL_BIN，请先编译（Cmd+F9）" >&2
    exit 1
fi

ssh root@"$BOARD_IP" "mkdir -p $REMOTE_DIR" || exit 1
scp -q "$LOCAL_BIN" root@"$BOARD_IP":"$REMOTE_DIR/" || exit 1
echo "=== 在 $BOARD_IP 上运行 $REMOTE_DIR/$BIN_NAME ==="
exec ssh root@"$BOARD_IP" "$REMOTE_DIR/$BIN_NAME $*"
