#!/bin/sh
# 把播放器装成板子桌面（正点原子 systemui）第 2 页上的 "TOnePlayer" 图标，以后不用电脑也能点开。
# 走 USB（adb），不需要板子 IP。改了代码以后再跑一次就是更新。
# 用法：./install_app_on_board_with_mac.sh
#
# 板子的桌面（2026-11-17 day7 实测）：开机脚本 /etc/init.d/S50systemui 启动 /opt/ui/systemui（正点原子用 Qt 写的桌面），
#   它启动时读 /opt/ui/src/ATK-DLRK3588/apk1~3.cfg：apk1 = 第 1 页，apk2 = 第 2 页，apk3 = 底部 Dock（只放得下 4 个）。
#   每行一个 app：appicons/<图标>.png <显示名> <程序名>，图标在 /opt/ui/src/appicons/，程序在 /opt/ui/src/apps/<程序名>
#
# 做的事情：
#   1. 编译
#   2. 程序、启动脚本、图标 → 板子 /userdata/apps/toneplayer/（/userdata 重启不会清空；/tmp 会）；
#      默认播放的视频不在板子上时提醒一下（不自动推，视频在 Mac 桌面上，不在仓库里）
#   3. 登记到 systemui：图标拷进 appicons/，apps/toneplayer 软链接到启动脚本，apk2.cfg 加一行（改之前备份一次）
#   4. 重启 systemui，让它重新读 apk2.cfg（只重启 systemui，不动 weston）
#
# 图标 assets/toneplayer_icon.png 是从 assets/toneplayer_icon_source.png 生成的：中间裁成正方形、缩到 256×256，
#   四个角切成半径 51 的圆角（透明）。桌面自带图标是 120×120（2026-11-19 看过 appicons/player.png），
#   但照片细节多，缩到 120 太糊（用户反馈），所以用 day7 NVR 图标同样的 256×256：
#   ffmpeg -i assets/toneplayer_icon_source.png -vf "crop='min(iw,ih)':'min(iw,ih)',scale=256:256:flags=lanczos,format=rgba,
#     geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lte(pow(max(max(51-X,X-(W-1-51)),0),2)+pow(max(max(51-Y,Y-(H-1-51)),0),2),51*51),255,0)'"
#     -frames:v 1 assets/toneplayer_icon.png
#
# 卸载：
#   adb shell "cd /opt/ui/src && sed -i '/ toneplayer\$/d' ATK-DLRK3588/apk2.cfg && rm -f apps/toneplayer appicons/toneplayer.png && rm -rf /userdata/apps/toneplayer"
#   然后重启板子（或执行下面第 4 步的那条命令）
# 可选环境变量：
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=day8_qt_player
BUILD_DIR=cmake-build-rk3588-debug
BOARD_APP_DIR=/userdata/apps/toneplayer
DEFAULT_MEDIA_PATH=/userdata/day8_media/yequ_640x480.mp4   # 和 main.cpp 的 kDefaultMediaPath 一样
SYSTEMUI_DIR=/opt/ui/src
SYSTEMUI_PAGE_CONFIG=$SYSTEMUI_DIR/ATK-DLRK3588/apk2.cfg
SYSTEMUI_APP_NAME=toneplayer        # systemui 里的程序名：apps/ 下的文件名、appicons/ 下的图标名都用它
SYSTEMUI_DISPLAY_NAME=TOnePlayer    # 图标下面显示的名字（cfg 用空格分列，名字里不能有空格）

cd "$(dirname "$0")" || exit 1

ADB=adb
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 第 1 步 编译（用 CLion 自带的 cmake；编译目录还没生成时先配置一次，和 run_on_board_with_mac.sh 一样）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_MAKE_PROGRAM=/Applications/CLion.app/Contents/bin/ninja/mac/aarch64/ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=/Users/dev/rk-toolchain/rk3588-toolchain.cmake || exit 1
fi
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 第 2 步 拷程序、启动脚本、图标。先停掉从桌面启动的旧程序（-f 按完整命令行找：只停 /userdata/apps/toneplayer 下的那个，
# 不动 run 脚本在 /tmp/CLion/run 下跑的），否则覆盖正在运行的程序文件可能失败。
#   匹配串写成 [/]userdata/...：pkill -f 也会匹配到执行这条命令的 shell 自己（它的命令行里也有这个路径），
#   把自己杀掉，后面的 mkdir 就不执行了（2026-11-19 实测踩坑）。正则里 [/] 只匹配 "/"，能匹配真正的程序路径，
#   而 shell 命令行里是字面的 "[/]userdata"，匹配不上，就不会误杀自己
# 所有 adb shell 都加 < /dev/null：不让 adb 读终端输入
$ADB shell "pkill -f '[/]userdata/apps/toneplayer/$BIN_NAME'; mkdir -p $BOARD_APP_DIR" < /dev/null
$ADB push "$BUILD_DIR/$BIN_NAME" "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push board/start_toneplayer.sh "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push assets/toneplayer_icon.png "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $BOARD_APP_DIR/$BIN_NAME $BOARD_APP_DIR/start_toneplayer.sh" < /dev/null || exit 1
# 这块板子的 adbd 不把板子上命令的退出码传回来（2026-11-18 实测），所以让板子 echo 结果，在 Mac 上比较文字
MEDIA_STATE=$($ADB shell "test -f $DEFAULT_MEDIA_PATH && echo yes || echo no" < /dev/null | tr -d '\r')
if [ "$MEDIA_STATE" != "yes" ]; then
    echo "提醒：板子上没有默认视频 $DEFAULT_MEDIA_PATH，点图标后会提示打不开文件。先 adb push 一个 mp4 到这个路径" >&2
fi

# 第 3 步 登记到 systemui
#   cp -p：连文件时间、权限一起拷；[ -f 备份 ] || ...：备份只做一次，再装不会把改过的文件当成原始文件覆盖备份
#   ln -sf：建软链接，systemui 启动 apps/toneplayer 时实际执行的是我们的启动脚本
#   grep -q 找到返回 0：已经登记过就不再加，重复安装不会出现两个图标
$ADB shell "
    cp $BOARD_APP_DIR/toneplayer_icon.png $SYSTEMUI_DIR/appicons/$SYSTEMUI_APP_NAME.png &&
    ln -sf $BOARD_APP_DIR/start_toneplayer.sh $SYSTEMUI_DIR/apps/$SYSTEMUI_APP_NAME &&
    { [ -f $SYSTEMUI_PAGE_CONFIG.bak_before_toneplayer ] || cp -p $SYSTEMUI_PAGE_CONFIG $SYSTEMUI_PAGE_CONFIG.bak_before_toneplayer; } &&
    { grep -q ' $SYSTEMUI_APP_NAME\$' $SYSTEMUI_PAGE_CONFIG ||
      echo 'appicons/$SYSTEMUI_APP_NAME.png $SYSTEMUI_DISPLAY_NAME $SYSTEMUI_APP_NAME' >> $SYSTEMUI_PAGE_CONFIG; }
" < /dev/null || exit 1

# 第 4 步 重启 systemui。
#   S50systemui 的 restart 只 killall systemui，不杀它带起来的音量条 sysvolume，再 start 会多出一个，所以先把两个都停掉再 start。
#   setsid：让 systemui 在新的会话里运行，adb shell 结束时不会把它一起带走
echo "=== 重启 systemui（板子桌面会刷新一下） ==="
$ADB shell "killall systemui sysvolume 2>/dev/null; sleep 0.5; setsid /etc/init.d/S50systemui start > /dev/null 2>&1" < /dev/null
sleep 3
# pidof 找到进程时输出 pid，找不到时什么都不输出（退出码传不回来），所以看输出是不是空的
SYSTEMUI_PID=$($ADB shell "pidof systemui" < /dev/null | tr -d '\r')
if [ -n "$SYSTEMUI_PID" ]; then
    echo "=== 装好了：板子桌面第 2 页上有 TOnePlayer 图标，点它启动；日志：adb shell cat $BOARD_APP_DIR/toneplayer.log ==="
else
    echo "systemui 没起来，在板子上执行 /etc/init.d/S50systemui start 或重启板子" >&2
    exit 1
fi
