#!/bin/sh
# 板子上用的启动脚本：在正点原子桌面（systemui）上点 NVR 图标时，systemui 执行 /opt/ui/src/apps/day7_nvr，
# 它是指向本脚本的软链接。由 install_app_on_board_with_mac.sh 装到 /userdata/apps/day7_qt_env/，不用手动拷。
#
# 为什么不直接启动程序：
#   1. 环境变量：被别的程序拉起来时，环境变量是对方给的，不一定有 QT_QPA_PLATFORM。
#      这里把连上 weston 要的 3 个变量都设好（含义见 run_on_board_with_mac.sh）。
#      已经设过的变量保持原值（${XXX:-默认值}：XXX 为空才用默认值）
#   2. 日志：每次启动在日志里记一行"开始"，程序结束后记一行"结束 + 退出码"，崩溃了能查到。
#      看日志：adb shell cat /userdata/apps/day7_qt_env/day7_qt_env.log

APP_DIR=/userdata/apps/day7_qt_env
LOG_FILE=$APP_DIR/day7_qt_env.log   # 放在 /userdata：板子重启后还在（/tmp 会清空）

# 先加载系统的环境变量（和板子的开机脚本 S49weston、S50systemui 一样）：时区 TZ=CST-8 在 /etc/profile.d/extra.sh 里，
# 不加载的话被别的程序以空环境拉起来时，日志里的时间会是 UTC，比北京时间慢 8 小时（2026-11-17 用 env -i 实测）
. /etc/profile

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export QT_QPA_PLATFORM=wayland

# >>：追加写。用 > 的话每次启动都会把上一次的日志清空，上一次崩溃的记录就没了
# &：程序放到后台运行；$!：刚放到后台的那个进程的进程号
"$APP_DIR/day7_qt_env" "$@" >> "$LOG_FILE" 2>&1 &
appPid=$!
echo "==== $(date '+%F %T') 开始 pid=$appPid 参数=[$*]" >> "$LOG_FILE"

# wait：等这个进程结束，$? 是它的退出码。
#   0     = 正常退出（点了「退出」或关了窗口）
#   1~127 = 程序自己 return / exit 的非 0 值（例如参数不认识返回 1）
#   >128  = 被信号杀掉，信号编号 = 退出码 - 128：
#           139 = 11 SIGSEGV（段错误，访问了非法内存，最常见的崩溃）
#           134 = 6  SIGABRT（abort，assert 失败、Qt 的 qFatal 都是这个）
#           143 = 15 SIGTERM（被 kill / pkill 正常结束，例如重新安装时）
wait "$appPid"
exitCode=$?
echo "==== $(date '+%F %T') 结束 pid=$appPid 退出码=$exitCode" >> "$LOG_FILE"
