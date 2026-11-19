#!/bin/sh
# 把 MppPlayer 装成板子桌面（正点原子 systemui）第 2 页上的 "MppPlayer" 图标，以后不用电脑也能点开。
# 走 USB（adb），不需要板子 IP。改了代码以后再跑一次就是更新。做法和 day8 的 TOnePlayer 一样。
# 用法：./install_app_on_board_with_mac.sh
#
# 板子的桌面（2026-11-17 day7 实测）：开机脚本 /etc/init.d/S50systemui 启动 /opt/ui/systemui，
#   它启动时读 /opt/ui/src/ATK-DLRK3588/apk1~3.cfg：apk1 = 第 1 页，apk2 = 第 2 页，apk3 = 底部 Dock。
#   每行一个 app：appicons/<图标>.png <显示名> <程序名>，图标在 /opt/ui/src/appicons/，程序在 /opt/ui/src/apps/<程序名>
#
# 做的事情：
#   1. 编译；推 FFmpeg 6.0 的库（push_ffmpeg6_to_board.sh）
#   2. 程序、启动脚本、图标 → 板子 /userdata/apps/mppplayer/
#   3. 登记到 systemui：图标拷进 appicons/，apps/mppplayer 软链接到启动脚本，apk2.cfg 加一行（改之前备份一次）
#   4. 重启 systemui，让它重新读 apk2.cfg
#
# 操作（板子没有键盘）：底部控制栏有进度条、播放时间 / 总时间、播放 / 暂停、退出；点画面显示 / 隐藏控制栏（见 src/ui/control_bar.h）
#
# 图标 assets/mppplayer_icon.png 是用 ffmpeg 画的：256×256，渐变底色 + 白色播放三角，四角半径 51 的圆角（透明）：
#   ffmpeg -f lavfi -i "color=c=black:s=256x256" -vf "format=rgba,
#     geq=r='if(gte(X,92)*lte(abs(Y-128),(188-X)*56/96),255,20+X*0.2)':g='if(gte(X,92)*lte(abs(Y-128),(188-X)*56/96),255,120+Y*0.3)':
#         b='if(gte(X,92)*lte(abs(Y-128),(188-X)*56/96),255,200-X*0.2)':
#         a='if(lte(pow(max(max(51-X,X-(W-1-51)),0),2)+pow(max(max(51-Y,Y-(H-1-51)),0),2),51*51),255,0)'"
#     -frames:v 1 assets/mppplayer_icon.png
#
# 卸载：
#   adb shell "cd /opt/ui/src && sed -i '/ mppplayer\$/d' ATK-DLRK3588/apk2.cfg && rm -f apps/mppplayer appicons/mppplayer.png && rm -rf /userdata/apps/mppplayer"
#   然后重启板子
# 可选环境变量：
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=MppPlayer
BUILD_DIR=cmake-build-rk3588-debug
BOARD_APP_DIR=/userdata/apps/mppplayer
DEFAULT_MEDIA_PATH=/userdata/day8_media/yequ_640x480.mp4   # 和 board/start_mppplayer.sh 里的一样
SYSTEMUI_DIR=/opt/ui/src
SYSTEMUI_PAGE_CONFIG=$SYSTEMUI_DIR/ATK-DLRK3588/apk2.cfg
SYSTEMUI_APP_NAME=mppplayer        # systemui 里的程序名：apps/ 下的文件名、appicons/ 下的图标名都用它
SYSTEMUI_DISPLAY_NAME=MppPlayer    # 图标下面显示的名字（cfg 用空格分列，名字里不能有空格）

cd "$(dirname "$0")" || exit 1

ADB=adb
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 第 1 步 编译（和 run_on_board_with_mac.sh 一样）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake || exit 1
fi
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" -j8 || exit 1

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi
./push_ffmpeg6_to_board.sh || exit 1

# 第 2 步 拷程序、启动脚本、图标。先停掉从桌面启动的旧程序，否则覆盖正在运行的程序文件可能失败。
#   匹配串写成 [/]userdata/...：pkill -f 也会匹配到执行这条命令的 shell 自己，[/] 让 shell 自己的命令行匹配不上
#   （2026-11-19 day8 实测踩坑）
$ADB shell "pkill -f '[/]userdata/apps/mppplayer/$BIN_NAME'; mkdir -p $BOARD_APP_DIR" < /dev/null
$ADB push "$BUILD_DIR/$BIN_NAME" "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push board/start_mppplayer.sh "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push assets/mppplayer_icon.png "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $BOARD_APP_DIR/$BIN_NAME $BOARD_APP_DIR/start_mppplayer.sh" < /dev/null || exit 1
# 板子 adbd 不回传退出码，让板子 echo 结果，在 Mac 上比较文字
MEDIA_STATE=$($ADB shell "test -f $DEFAULT_MEDIA_PATH && echo yes || echo no" < /dev/null | tr -d '\r')
if [ "$MEDIA_STATE" != "yes" ]; then
    echo "提醒：板子上没有默认视频 $DEFAULT_MEDIA_PATH，点图标后会打不开文件。先 adb push 一个 mp4 到这个路径" >&2
fi

# 第 3 步 登记到 systemui（备份只做一次；已经登记过就不再加）
$ADB shell "
    cp $BOARD_APP_DIR/mppplayer_icon.png $SYSTEMUI_DIR/appicons/$SYSTEMUI_APP_NAME.png &&
    ln -sf $BOARD_APP_DIR/start_mppplayer.sh $SYSTEMUI_DIR/apps/$SYSTEMUI_APP_NAME &&
    { [ -f $SYSTEMUI_PAGE_CONFIG.bak_before_mppplayer ] || cp -p $SYSTEMUI_PAGE_CONFIG $SYSTEMUI_PAGE_CONFIG.bak_before_mppplayer; } &&
    { grep -q ' $SYSTEMUI_APP_NAME\$' $SYSTEMUI_PAGE_CONFIG ||
      echo 'appicons/$SYSTEMUI_APP_NAME.png $SYSTEMUI_DISPLAY_NAME $SYSTEMUI_APP_NAME' >> $SYSTEMUI_PAGE_CONFIG; }
" < /dev/null || exit 1

# 第 4 步 重启 systemui（S50systemui 的 restart 不杀 sysvolume，会多出一个，所以两个都先停掉再 start）
echo "=== 重启 systemui（板子桌面会刷新一下） ==="
$ADB shell "killall systemui sysvolume 2>/dev/null; sleep 0.5; setsid /etc/init.d/S50systemui start > /dev/null 2>&1" < /dev/null
sleep 3
SYSTEMUI_PID=$($ADB shell "pidof systemui" < /dev/null | tr -d '\r')
if [ -n "$SYSTEMUI_PID" ]; then
    echo "=== 装好了：板子桌面第 2 页上有 MppPlayer 图标，点它启动；日志：adb shell cat $BOARD_APP_DIR/mppplayer.log ==="
else
    echo "systemui 没起来，在板子上执行 /etc/init.d/S50systemui start 或重启板子" >&2
    exit 1
fi
