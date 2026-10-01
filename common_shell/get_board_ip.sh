#!/bin/sh
# 通过 adb（OTG 的 USB 线）获取 RK3588 开发板的 IP，不依赖网络。所有工程共用。
#
# 用法：
#   ./get_board_ip.sh                      # 只输出 IP，例如 192.168.1.5
#   BOARD_IP=$(./get_board_ip.sh)          # 在别的脚本里取 IP
#
# 可选环境变量：
#   ADB=/path/to/adb    adb 不在 PATH 里时指定（Mac 上默认会去找 ~/Documents/Android_Env/sdk/platform-tools/adb）
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）
#   BOARD_IFACES="wlan0 eth0 eth1"   按顺序查找的网卡，默认先 Wi-Fi 后有线
#
# 返回值：0 成功（标准输出只有 IP），1 失败（错误信息输出到标准错误）

IFACES=${BOARD_IFACES:-"wlan0 eth0 eth1"}

# 1. 找 adb
if [ -z "$ADB" ]; then
    if command -v adb >/dev/null 2>&1; then
        ADB=adb
    elif [ -x "$HOME/Documents/Android_Env/sdk/platform-tools/adb" ]; then
        ADB="$HOME/Documents/Android_Env/sdk/platform-tools/adb"
    else
        echo "get_board_ip: 找不到 adb（Ubuntu: sudo apt install adb；或设置 ADB=adb的路径）" >&2
        exit 1
    fi
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 2. 确认板子连上了（第一次会顺带启动 adb 后台服务，这样就不用执行两次）
$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "get_board_ip: adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 3. 按顺序查网卡的 IPv4 地址（tr -d '\r' 去掉 adb 输出里可能带的回车符）
for IFACE in $IFACES; do
    IP=$($ADB shell "ip -4 addr show $IFACE 2>/dev/null" 2>/dev/null | tr -d '\r' \
         | awk '/inet /{split($2, a, "/"); print a[1]; exit}')
    if [ -n "$IP" ]; then
        echo "$IP"
        exit 0
    fi
done

echo "get_board_ip: 板子上的 $IFACES 都没有 IP。板子连上 Wi-Fi 了吗？（connmanctl 连接方法见 all_step_recoder/05_板子调试/02）" >&2
exit 1
