#!/bin/sh
# 获取 RK3588 开发板的 IP（调用公共脚本 rk34588_work/common_shell/get_board_ip.sh）
# 用法：./run_get_board_ip.sh

cd "$(dirname "$0")" || exit 1
GET_BOARD_IP=../../common_shell/get_board_ip.sh

BOARD_IP=$("$GET_BOARD_IP") || exit 1
echo "板子 IP  ： $BOARD_IP"
echo "登录板子 ： ssh root@$BOARD_IP"
