#!/bin/sh
# 在 Mac 上用 aaa.264（约 30 fps、1803 帧、约 60 秒）转出 M5 要的两路不同帧率的输入，再 adb push 到板子。
# 用法：./make_m5_inputs.sh [aaa.264 的路径]   默认 /Users/dev/Documents/AV/rk_test_data/aaa.264
#
# 本机 ffmpeg（/usr/local/ffmpeg/4.4）没有 libx264，只能用 Mac 的硬件编码器 h264_videotoolbox。
# 2026-11-20 实测：
#   - 直接 -vf fps=15 不行：aaa.264 是裸码流，时间戳乱（ffmpeg 报 pts < dts），fps 滤镜按乱的时间戳补帧，输出变成几百 MB
#   - 改成按“第几帧”重新算时间戳 setpts=N/(帧率*TB) 以后正常：
#       n0_ch1_15fps.h264  902 帧（隔一帧取一帧），约 30 MB
#       n0_ch2_60fps.h264  3606 帧（每帧复制一次），约 30 MB
#   - ffprobe 显示 has_b_frames=2（videotoolbox 的 High profile 留了重排序深度），MPP 解出来已经是显示顺序，不影响按输出顺序打时间戳

INPUT=${1:-/Users/dev/Documents/AV/rk_test_data/aaa.264}
OUTPUT_DIR=$(dirname "$INPUT")
FFMPEG=/usr/local/ffmpeg/4.4/bin/ffmpeg
FFPROBE=/usr/local/ffmpeg/4.4/bin/ffprobe

# 15 fps：select 只留偶数帧（n 是第几帧，mod(n,2)=0），setpts 按新的序号重排时间戳；passthrough：不补帧、不丢帧
"$FFMPEG" -y -v error -i "$INPUT" -vf "select='not(mod(n\,2))',setpts=N/(15*TB)" -vsync passthrough \
    -c:v h264_videotoolbox -b:v 4M -bf 0 -f h264 "$OUTPUT_DIR/n0_ch1_15fps.h264" || exit 1

# 60 fps：先按 30 fps 重排时间戳，再 -r 60 -vsync cfr 输出恒定 60 fps，每帧被复制一次
"$FFMPEG" -y -v error -i "$INPUT" -vf "setpts=N/(30*TB)" -r 60 -vsync cfr \
    -c:v h264_videotoolbox -b:v 4M -bf 0 -f h264 "$OUTPUT_DIR/n0_ch2_60fps.h264" || exit 1

for f in n0_ch1_15fps.h264 n0_ch2_60fps.h264; do
    "$FFPROBE" -v error -count_frames -select_streams v:0 \
        -show_entries stream=codec_name,width,height,nb_read_frames -of compact "$OUTPUT_DIR/$f"
done

# 推到板子（走 USB）。板子不在线时只生成文件，不报错退出
if adb get-state >/dev/null 2>&1; then
    adb push "$OUTPUT_DIR/n0_ch1_15fps.h264" "$OUTPUT_DIR/n0_ch2_60fps.h264" /userdata/av/
    adb shell "ls -l /userdata/av/n0_ch*" < /dev/null
else
    echo "板子不在线：文件已生成在 $OUTPUT_DIR，接上板子后再 adb push 到 /userdata/av/"
fi
