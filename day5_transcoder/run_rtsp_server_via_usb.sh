#!/bin/sh
# M3：在板子上启动 live555MediaServer（RTSP 点播服务器），并用 adb forward 把它的端口转到 Mac。
# 全程走 USB（adb），不用板子 IP。
#
# 用法：./run_rtsp_server_via_usb.sh
# 然后在 Mac 的另一个终端播放：ffplay -rtsp_transport tcp rtsp://localhost:8554/out_720p.265
#
# 停止：Ctrl+C 只能停掉 Mac 这边的 adb，板子上的 live555 可能还在跑。
#      下次运行本脚本时会先把旧的停掉；也可以手动 adb shell "killall live555MediaServer"

MEDIA_DIR=/userdata/av   # live555 把"启动时的当前目录"里的文件当点播源，转码结果就在这里
LOCAL_RTSP_PORT=8554     # Mac 这边的端口：Mac 普通用户不能监听 1024 以下的端口
BOARD_RTSP_PORT=554      # 板子上 live555 监听的端口：RTSP 标准端口，adb shell 是 root 能拿到（2026-11-17 实测）

# 第 1 步 确认板子连上了
# adb get-state：有设备时打印 device 并返回 0，没有时返回非 0
if ! adb get-state >/dev/null 2>&1; then
    echo "adb 没找到板子：检查板子开机了吗、OTG 线（TypeC0 口）插好了吗，执行 adb devices 看看" >&2
    exit 1
fi

# 第 2 步 建 .265 结尾的链接：live555 按扩展名认格式，认 .265 不认 .h265（.h265 会返回 404）
# 所有 adb shell 都加 < /dev/null：不让 adb 读终端输入，否则一次粘贴多行命令时，后面几行会被 adb 吃掉
adb shell "cd $MEDIA_DIR && ln -sf out_720p.h265 out_720p.265 && ln -sf out_720p_m2.h265 out_720p_m2.265" < /dev/null || exit 1

# 第 3 步 把上次没停掉的 live555 停掉，否则 554 端口被占着，新的起不来
# 没有在跑的 live555 时 killall 会报错，丢掉输出、不当成失败
adb shell "killall live555MediaServer" < /dev/null >/dev/null 2>&1

# 第 4 步 端口转发：Mac 的 localhost:8554 ←USB→ 板子的 554
# adb forward 只转 TCP，所以播放器要用"RTP over TCP"（RTSP 命令和视频数据走同一条 TCP 连接）
adb forward tcp:$LOCAL_RTSP_PORT tcp:$BOARD_RTSP_PORT >/dev/null || exit 1
echo "端口转发：Mac localhost:$LOCAL_RTSP_PORT → 板子 $BOARD_RTSP_PORT"

# 第 5 步 在 MEDIA_DIR 里启动 live555（板子自带，在 /usr/bin 下）；前台运行，日志直接显示在这个终端
# exec：用 adb 这个进程替换掉脚本本身，Ctrl+C 直接作用在 adb 上
echo "=== 板子上启动 live555MediaServer（目录 $MEDIA_DIR）==="
echo "    启动日志里的 rtsp://<板子IP>/ 不要用；Mac 上一律连 rtsp://localhost:$LOCAL_RTSP_PORT/文件名"
exec adb shell "cd $MEDIA_DIR && live555MediaServer" < /dev/null
