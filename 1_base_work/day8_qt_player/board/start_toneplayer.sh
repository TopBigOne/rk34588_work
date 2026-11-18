#!/bin/sh
# 板子上用的启动脚本：在正点原子桌面（systemui）上点 TOnePlayer 图标时，systemui 执行 /opt/ui/src/apps/toneplayer，
# 它是指向本脚本的软链接。由 install_app_on_board_with_mac.sh 装到 /userdata/apps/toneplayer/，不用手动拷。
#
# 为什么不直接启动程序：
#   1. 环境变量：被 systemui 拉起来时，环境变量是 systemui 给的，不一定有连 weston、找 MPP 库要的那几个，这里都设好。
#      已经设过的变量保持原值（${XXX:-默认值}：XXX 为空才用默认值）
#   2. 日志：每次启动记一行"开始"，程序结束后记一行"结束 + 退出码"，崩溃了能查到。
#      看日志：adb shell cat /userdata/apps/toneplayer/toneplayer.log
# 不带参数启动，程序播默认视频（main.cpp 里的 kDefaultMediaPath）

APP_DIR=/userdata/apps/toneplayer
LOG_FILE=$APP_DIR/toneplayer.log   # 放在 /userdata：板子重启后还在（/tmp 会清空）

# 先加载系统的环境变量（和开机脚本 S49weston、S50systemui 一样），时区 TZ=CST-8 在这里面，
# 不加载的话日志里的时间是 UTC，比北京时间慢 8 小时（2026-11-17 day7 实测）
. /etc/profile

# 连 weston 要的 3 个变量（2026-11-17 day7 实测：weston 的 socket 是 /var/run/wayland-0）
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export QT_QPA_PLATFORM=wayland
# 告诉程序"是从正点原子桌面启动的"：程序看到它，就请 systemui 把桌面藏起来，否则约 1 秒后桌面会盖住播放器。
# 值是 apk2.cfg 第 3 列的程序名，systemui 靠它认出是哪个程序（见 src/ui/systemui_client.h）
export SYSTEMUI_APP_NAME=toneplayer
# 优先用自己编译的新版 MPP 库（出厂的 /usr/lib 里那份太旧）。程序里也写了 rpath，这里双保险
export LD_LIBRARY_PATH=/userdata/mpp_build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# >>：追加写，上一次崩溃的记录不会被清掉
# &：程序放到后台运行；$!：刚放到后台的那个进程的进程号
"$APP_DIR/day8_qt_player" "$@" >> "$LOG_FILE" 2>&1 &
appPid=$!
echo "==== $(date '+%F %T') 开始 pid=$appPid 参数=[$*]" >> "$LOG_FILE"

# wait：等这个进程结束，$? 是它的退出码。
#   0     = 正常退出（点了「退出」）
#   1~127 = 程序自己 return 的非 0 值
#   >128  = 被信号杀掉，信号编号 = 退出码 - 128：139 = 段错误，134 = abort，143 = 被 kill（例如重新安装时）
wait "$appPid"
exitCode=$?
echo "==== $(date '+%F %T') 结束 pid=$appPid 退出码=$exitCode" >> "$LOG_FILE"
