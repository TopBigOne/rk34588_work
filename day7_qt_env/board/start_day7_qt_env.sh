#!/bin/sh
# 板子上用的启动脚本：weston 桌面上点 day7 NVR 图标时，weston 执行的就是这个脚本。
# 由 install_app_on_board_with_mac.sh 装到 /userdata/apps/day7_qt_env/，不用手动拷。
#
# 为什么不直接让图标启动程序：从 weston 桌面启动的程序，环境变量是 weston 给的，不一定有 QT_QPA_PLATFORM。
# 这里把连上 weston 要的 3 个变量都设好（含义见 run_on_board_with_mac.sh），再启动程序。
# 已经设过的变量保持原值（${XXX:-默认值}：XXX 为空才用默认值）

APP_DIR=/userdata/apps/day7_qt_env

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export QT_QPA_PLATFORM=wayland

# exec：用程序替换掉这个 shell 进程，少一个进程；程序的输出写到日志里，出问题时用 adb shell cat 看
exec "$APP_DIR/day7_qt_env" "$@" > /tmp/day7_qt_env.log 2>&1
