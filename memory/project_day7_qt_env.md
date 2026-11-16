---
name: project-day7-qt-env
description: 2026-11-17：Mac + CLion 给板子开发 Qt 的环境已搭好；NVR 2×2 界面 + 装到正点原子桌面 systemui 第 2 页；代码和脚本已提交（7f87eb2、a9597b1）
metadata:
  type: project
---

- 目录 `day7_qt_env/`，分支 `day7_qt_env`（用户 2026-11-17 自己建的）。提交：7f87eb2 代码、a9597b1 脚本，前缀 `[day7_qt_env]`。暂存区里有用户的 day6 文件，提交时用 `git commit -- 路径` 只提 day7 的，没动它们。
- 结论：板子 Buildroot 带 Qt 5.15.8（Widgets/Gui/Quick/Multimedia/WaylandClient，`/usr/lib/qt/plugins/platforms/libqwayland-egl.so`），**没有 Qt6**；`/usr/lib/qt/qml` 不存在，先用 Widgets。Mac 上 Homebrew 的 qtbase 6.11.1 只能编 Mac 程序，不能给板子用；Mac 上没装 qt@5。
- 板子屏幕上看到的桌面（相机、音乐……）是正点原子的 Qt 程序 /opt/ui/systemui，不是 weston 画的，详见下面"桌面图标"。（早先我以为是 weston 01-launcher.ini 里的 [launcher]，错的。）
- 一次性准备（已做）：
  - `~/rk-toolchain/mac-bin/` 加了 qmake、moc、uic、rcc 软链接 → rk-tool-wrapper（转发到 OrbStack rk-dev）
  - `~/rk-toolchain/rk3588-toolchain.cmake` 末尾提前定义 `Qt5::qmake/moc/uic/rcc` 指向软链接，原因：sysroot 里的 Qt5CoreConfigExtras.cmake 把工具路径写死成 `/opt/atk-dlrk3588-toolchain/bin/xxx`，Mac 上不存在，find_package 会报错。备份 `rk3588-toolchain.cmake.bak_before_qt`。day5 用新工具链重编通过。
- 板子上运行要设 `XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland`（weston 进程环境里实测是 /var/run）；脚本 `day7_qt_env/run_on_board_with_mac.sh`。
- 测试程序 ClickCounterWindow（文字 + 按钮计数），2026-11-17 用户确认窗口出来、按钮能计数。
- 2026-11-17 用户配好了 CLion 的 CMake profile（只填 -DCMAKE_TOOLCHAIN_FILE=rk3588-toolchain.cmake + build dir cmake-build-rk3588-debug），红线消失。用户一开始以为要把 Qt5WidgetsConfig.cmake 填进 CMAKE_TOOLCHAIN_FILE —— 工具链文件和 find_package 找包是两回事，讲文档时要说清。
- 2026-11-17 做了 NVR 2×2 + 一排按钮（VideoTileWidget 用 heightForWidth 保持 16:9，NvrPreviewWindow：QVBoxLayout 套 QGridLayout + QHBoxLayout + addStretch；单画面靠 setVisible）。屏幕是 DSI 竖屏 1080×1920。用 scratchpad 里的 grab() 程序在板子上截图确认过布局。
- 桌面图标（2026-11-17 踩坑后改对）：板子桌面是开机脚本 S50systemui 启动的 /opt/ui/systemui（正点原子 Qt 桌面，相机/音乐等图标），全屏盖在 weston 上；weston 自己的桌面是空的。**重启 weston 会把 systemui 一起关掉且不会自动恢复**（用户看到"其他图标都没了"），我一开始误加了 weston 的 [desktop-launcher]，已撤掉。
  - 正确方式：systemui 读 /opt/ui/src/ATK-DLRK3588/apk1~3.cfg（apk1 第 1 页、apk2 第 2 页、apk3 = Dock 只放得下 4 个，放第 5 个会溢出），每行 `appicons/x.png 显示名 程序名`，程序 = /opt/ui/src/apps/程序名。
  - `install_app_on_board_with_mac.sh` 现在：装到 /userdata/apps/day7_qt_env/，appicons/day7_nvr.png，apps/day7_nvr → 启动脚本软链接，apk2.cfg 加一行（备份 apk2.cfg.bak_before_day7），只重启 systemui（先 killall systemui sysvolume）。实测从图标点开的 NVR 父进程是 systemui。
  - 截屏方法：weston 要带 --debug 启动才允许 weston-screenshooter（否则 unauthorized）。2026-11-17 板子上的 weston 是我用 --debug 启动的，重启板子后恢复正常。
  - 用户一次点了 4 下图标，开了 4 个 NVR（02:24:53～02:25:05），原因待问（可能点了没反应）。
- 还没做：gdbserver 调试 Qt 程序、Mac 本机预览配置。
- 和主线的关系：以后 NVR 的界面可以用 Qt 做，视频仍走 MPP 解码 + RGA 拼接。
