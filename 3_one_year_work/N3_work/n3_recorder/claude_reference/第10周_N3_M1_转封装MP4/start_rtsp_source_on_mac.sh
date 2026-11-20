#!/bin/sh
# 给 M1 准备一路 RTSP 流（代替跳过的 N1）：Mac 上跑 SRS 7，ffmpeg 循环推 aaa.264，板子通过 adb reverse 拉。
# 用法：./start_rtsp_source_on_mac.sh        （Ctrl+C 停推流；SRS 用 ./objs/srs -c conf/rtsp.conf 另开终端起）
# 然后：./run_on_board_with_mac.sh m1 rtsp://127.0.0.1:8554/live/ch0
# 状态：2026-11-20 写好，没跑过：SRS 还没在 Mac 上编译。rtsp.conf 第 27 行 listen 8554 已核对；URL 路径 /live/ch0 是推断（SRS 惯例 /app/stream）
SRS_DIR=/Users/dev/Documents/AV/srs/trunk
INPUT=${1:-/Users/dev/Documents/AV/rk_test_data/aaa.264}
FFMPEG=/usr/local/ffmpeg/4.4/bin/ffmpeg

[ -x "$SRS_DIR/objs/srs" ] || { echo "先编译 SRS：cd $SRS_DIR && ./configure && make" >&2; exit 1; }
adb reverse tcp:8554 tcp:8554 || exit 1   # 板子 127.0.0.1:8554 → Mac 8554
# -re：按原速读；-stream_loop -1：无限循环；-c copy：不转码；裸流没有时间戳，-r 30 按 30 fps 造
exec "$FFMPEG" -re -stream_loop -1 -r 30 -i "$INPUT" -c copy -f flv rtmp://127.0.0.1/live/ch0
