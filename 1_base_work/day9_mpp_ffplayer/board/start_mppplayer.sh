#!/bin/sh
# 板子上用的启动脚本：在正点原子桌面（systemui）上点 MppPlayer 图标时，systemui 执行 /opt/ui/src/apps/mppplayer，
# 它是指向本脚本的软链接。由 install_app_on_board_with_mac.sh 装到 /userdata/apps/mppplayer/，不用手动拷。
#
# 和 day8 TOnePlayer 不一样的地方：
#   TOnePlayer 是 Qt 程序，用 Qt Remote Objects 告诉 systemui “我在前台”，systemui 就不会把桌面盖回来。
#   MppPlayer 是 SDL 写的 C 程序，没有这套通信，所以换个办法：播放期间先把 systemui（和它带起来的音量条 sysvolume）停掉，
#   播完再重新启动它。代价是播放期间音量键不起作用，退出后桌面要 2～3 秒才出来。
#
# 为什么要自己再起一个后台进程（第 1 步）：
#   本脚本是 systemui 拉起来的。停 systemui 时，Qt 的 QProcess 在析构时会把它启动的子进程（也就是本脚本）一起杀掉。
#   所以先用 setsid 把真正干活的部分放到一个新的会话里运行（不再是 systemui 的子进程），外层脚本马上退出。
#
# 日志：adb shell cat /userdata/apps/mppplayer/mppplayer.log
# 播放的视频：带参数时用参数（ffplay 参数 + 视频路径），不带参数时播 DEFAULT_MEDIA_PATH

APP_DIR=/userdata/apps/mppplayer
LOG_FILE=$APP_DIR/mppplayer.log   # 放在 /userdata：板子重启后还在（/tmp 会清空）
DEFAULT_MEDIA_PATH=/userdata/day8_media/yequ_640x480.mp4

# 第 1 步 外层：换到新会话里重新执行自己（带上 --detached 标记），然后马上退出
if [ "$1" != "--detached" ]; then
    # setsid：在新的会话里运行，和 systemui 没有父子关系；< /dev/null 和重定向：不占用 systemui 给的输入输出
    setsid "$0" --detached "$@" < /dev/null >> "$LOG_FILE" 2>&1 &
    exit 0
fi
shift # 去掉 --detached，剩下的是用户参数

# 先加载系统的环境变量（和开机脚本 S49weston、S50systemui 一样），时区 TZ=CST-8 在这里面，
# 不加载的话日志里的时间是 UTC，比北京时间慢 8 小时（2026-11-17 day7 实测）
. /etc/profile

# 连 weston 要的变量（2026-11-17 day7 实测：weston 的 socket 是 /var/run/wayland-0）；SDL 用 Wayland 显示
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export SDL_VIDEODRIVER=wayland
# 程序里已经用 rpath 写了这两个目录，这里双保险
export LD_LIBRARY_PATH=/userdata/ffmpeg6/lib:/userdata/mpp_build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# 第 2 步 停掉桌面（systemui 和音量条 sysvolume），免得它把播放器盖住
killall systemui sysvolume 2>/dev/null

# 第 3 步 播放。没给参数时：全屏（-fs）、播完自动退出（-autoexit）、播默认视频
if [ $# -eq 0 ]; then
    set -- -fs -autoexit "$DEFAULT_MEDIA_PATH"
fi
echo "==== $(date '+%F %T') 开始 参数=[$*]"
"$APP_DIR/MppPlayer" "$@"
# $? 是退出码：0 = 正常退出；123 = 收到 SIGTERM（ffplay 的 sigterm_handler）；>128 = 被信号杀掉，139 = 段错误
exitCode=$?
echo "==== $(date '+%F %T') 结束 退出码=$exitCode"

# 第 4 步 把桌面重新启动起来
/etc/init.d/S50systemui start > /dev/null 2>&1
