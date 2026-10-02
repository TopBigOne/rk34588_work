#!/bin/sh
cd "$(dirname "$0")" || exit 1
GET_BOARD_IP=../common_shell/get_board_ip.sh

BOARD_IP=$("$GET_BOARD_IP") || exit 1
ssh root@"${BOARD_IP}" 'mkdir -p /userdata/av && df -h /userdata'


