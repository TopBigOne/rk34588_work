#!/bin/sh
# 在 Mac 上用 ffmpeg 生成播放器的测试视频：test_media/sync_test_1080p.mp4
#   画面：1920×1080、30 fps、60 秒、H.264（VideoToolbox 硬件编码）。testsrc 图案自带一个每秒加 1 的大数字；
#         每到整秒，画面上方出现一条白色横条，持续 0.1 秒（3 帧）
#   声音：AAC 48000 Hz 双声道，1000 Hz 的正弦波，每到整秒"嘀"一声（sine 滤镜的 beep_factor：
#         每秒开头加一小段 beep_factor 倍频率的音）
# 白条和"嘀"同时出现 → 在板子上看、听，就知道音画是不是同步
#
# 用法：./make_test_media.sh
# 生成后：adb push test_media/sync_test_1080p.mp4 /userdata/day8_media/

cd "$(dirname "$0")" || exit 1
mkdir -p test_media

# -f lavfi -i "滤镜"：不读文件，用滤镜现场生成画面 / 声音
#   testsrc=size=1920x1080:rate=30：测试图案（色条 + 每秒加 1 的数字）
#   drawbox=...:enable='lt(mod(t,1),0.1)'：t 是当前秒数，mod(t,1) < 0.1 的那 0.1 秒里画白条（t=fill 实心）
#   sine=frequency=1000:beep_factor=4：1000 Hz 正弦波，每秒开头一声 4000 Hz 的"嘀"
# -t 60：只要 60 秒
# -c:v h264_videotoolbox -profile:v high -b:v 8M：Mac 的硬件 H.264 编码器（这台 Mac 的 ffmpeg 没带 libx264）
# -pix_fmt yuv420p：最常见的像素格式，所有播放器都认
# -g 60：每 60 帧（2 秒）一个关键帧，拖进度条时最多从 2 秒前开始解
# -c:a aac -b:a 128k -ar 48000 -ac 2：AAC、128 kbps、48000 Hz、双声道
# -movflags +faststart：把 mp4 的索引（moov）挪到文件开头，打开更快
ffmpeg -y \
    -f lavfi -i "testsrc=size=1920x1080:rate=30,drawbox=x=0:y=0:w=iw:h=120:color=white:t=fill:enable='lt(mod(t\,1)\,0.1)'" \
    -f lavfi -i "sine=frequency=1000:beep_factor=4:sample_rate=48000" \
    -t 60 \
    -c:v h264_videotoolbox -profile:v high -b:v 8M -pix_fmt yuv420p -g 60 \
    -c:a aac -b:a 128k -ar 48000 -ac 2 \
    -movflags +faststart \
    test_media/sync_test_1080p.mp4 || exit 1

ffprobe -v error -show_entries stream=codec_name,profile,width,height,r_frame_rate,sample_rate,channels,nb_frames \
    -of compact test_media/sync_test_1080p.mp4
