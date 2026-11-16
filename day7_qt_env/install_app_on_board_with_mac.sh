#!/bin/sh
# 把 day7_qt_env 装成板子桌面（systemui）第 2 页上的一个"NVR"图标，以后不用电脑也能点开。
# 走 USB（adb），不需要板子 IP。改了代码以后再跑一次就是更新。
# 用法：./install_app_on_board_with_mac.sh
#
# 板子的桌面是谁画的（2026-11-17 实测）：
#   开机脚本 /etc/init.d/S50systemui 启动 /opt/ui/systemui（正点原子用 Qt 写的桌面，相机、音乐那些图标都是它画的），
#   它全屏盖在 weston 上面。weston 自己的桌面是空的（02-desktop.ini 关了任务栏，[desktop-launcher] 全被注释）。
#   systemui 启动时读 /opt/ui/src/ATK-DLRK3588/apk1~3.cfg：apk1 = 第 1 页，apk2 = 第 2 页，apk3 = 底部 Dock（只放得下 4 个）。
#   每行一个 app：appicons/<图标>.png <显示名> <程序名>，图标在 /opt/ui/src/appicons/，程序在 /opt/ui/src/apps/<程序名>
#adb shell cat /userdata/apps/day7_qt_env/day7_qt_env.log
# 做的事情：
#   1. 编译
#   2. 程序、启动脚本、图标 → 板子 /userdata/apps/day7_qt_env/（/userdata 是数据分区，重启不会清空；/tmp 会）
#   3. 登记到 systemui：图标拷进 appicons/，apps/day7_nvr 软链接到启动脚本，apk2.cfg 加一行（改之前备份一次）
#   4. 重启 systemui，让它重新读 apk2.cfg（只重启 systemui，不动 weston）
#
# 卸载：
#   adb shell "cd /opt/ui/src && cp ATK-DLRK3588/apk2.cfg.bak_before_day7 ATK-DLRK3588/apk2.cfg && rm -f apps/day7_nvr appicons/day7_nvr.png && rm -rf /userdata/apps/day7_qt_env"
#   然后重启板子（或执行下面第 4 步的那条命令）
# 可选环境变量：
#   ADB=/path/to/adb    不用 PATH 里的 adb 时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

BIN_NAME=day7_qt_env
BUILD_DIR=cmake-build-rk3588-debug
BOARD_APP_DIR=/userdata/apps/day7_qt_env
SYSTEMUI_DIR=/opt/ui/src
SYSTEMUI_PAGE_CONFIG=$SYSTEMUI_DIR/ATK-DLRK3588/apk2.cfg
SYSTEMUI_APP_NAME=day7_nvr          # systemui 里的程序名：apps/ 下的文件名、appicons/ 下的图标名都用它
SYSTEMUI_DISPLAY_NAME=NVR           # 图标下面显示的名字

cd "$(dirname "$0")" || exit 1

ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里，或者设置 ADB=adb的路径" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

# 第 1 步 编译（用 CLion 自带的 cmake，编译目录就是 CLion 的 RK3588 配置生成的那个）
CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

# 第 2 步 拷程序、启动脚本、图标。先停掉正在运行的旧程序（-x：进程名完全一样才停），否则覆盖正在运行的程序文件可能失败
# 所有 adb shell 都加 < /dev/null：不让 adb 读终端输入（原因见 day5_transcoder/run_on_board_with_mac.sh）
$ADB shell "pkill -x $BIN_NAME; mkdir -p $BOARD_APP_DIR" < /dev/null
$ADB push "$BUILD_DIR/$BIN_NAME" "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push board/start_day7_qt_env.sh "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB push assets/nvr_icon.png "$BOARD_APP_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $BOARD_APP_DIR/$BIN_NAME $BOARD_APP_DIR/start_day7_qt_env.sh" < /dev/null || exit 1

# 第 3 步 登记到 systemui
#   cp -p：连文件时间、权限一起拷；[ -f 备份 ] || ...：备份只做一次，再装不会把改过的文件当成原始文件覆盖备份
#   ln -sf：建软链接，systemui 启动 apps/day7_nvr 时实际执行的是我们的启动脚本
#   grep -q 找到返回 0：已经登记过就不再加，重复安装不会出现两个图标
$ADB shell "
    cp $BOARD_APP_DIR/nvr_icon.png $SYSTEMUI_DIR/appicons/$SYSTEMUI_APP_NAME.png &&
    ln -sf $BOARD_APP_DIR/start_day7_qt_env.sh $SYSTEMUI_DIR/apps/$SYSTEMUI_APP_NAME &&
    { [ -f $SYSTEMUI_PAGE_CONFIG.bak_before_day7 ] || cp -p $SYSTEMUI_PAGE_CONFIG $SYSTEMUI_PAGE_CONFIG.bak_before_day7; } &&
    { grep -q ' $SYSTEMUI_APP_NAME\$' $SYSTEMUI_PAGE_CONFIG ||
      echo 'appicons/$SYSTEMUI_APP_NAME.png $SYSTEMUI_DISPLAY_NAME $SYSTEMUI_APP_NAME' >> $SYSTEMUI_PAGE_CONFIG; }
" < /dev/null || exit 1

# 第 4 步 重启 systemui。
#   S50systemui 的 restart 只 killall systemui，不杀它带起来的音量条 sysvolume，再 start 会多出一个，所以先把两个都停掉再 start。
#   setsid：让 systemui 在新的会话里运行，和这次 adb shell 脱离关系，adb shell 结束时不会把它一起带走
echo "=== 重启 systemui（板子桌面会刷新一下） ==="
$ADB shell "killall systemui sysvolume 2>/dev/null; sleep 0.5; setsid /etc/init.d/S50systemui start > /dev/null 2>&1" < /dev/null
sleep 3
if $ADB shell "pidof systemui" < /dev/null >/dev/null 2>&1; then
    echo "=== 装好了：板子桌面第 2 页上有 NVR 图标，点它启动；日志：adb shell cat /userdata/apps/day7_qt_env/day7_qt_env.log ==="
else
    echo "systemui 没起来，在板子上执行 /etc/init.d/S50systemui start 或重启板子" >&2
    exit 1
fi
