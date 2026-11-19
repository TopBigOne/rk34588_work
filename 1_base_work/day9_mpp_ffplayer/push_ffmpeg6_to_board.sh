#!/bin/sh
# 把交叉编译好的 FFmpeg 6.0 动态库推到板子的 /userdata/ffmpeg6/lib（MppPlayer 的 rpath 指向这里）。
# 板子上已经是同一份就跳过（比较 libavcodec 的 md5），所以 run / install 脚本每次都可以放心调用。
# 用法：./push_ffmpeg6_to_board.sh          （先运行过 ./build_ffmpeg6_for_board.sh）
# 可选环境变量：
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

FFMPEG6_LIB_DIR=/Users/dev/rk-toolchain/ffmpeg6-rk3588/lib
BOARD_FFMPEG6_LIB_DIR=/userdata/ffmpeg6/lib   # /userdata 重启不会清空（/tmp 会）

# 程序运行时按 soname 找库（readelf -d MppPlayer 里 NEEDED 那几行），所以只推 libxxx.so.<主版本号> 这 8 个。
# 它们在本地是软链接（libavcodec.so.60 → libavcodec.so.60.3.100），adb push 推软链接时推的是它指向的文件内容，
# 板子上得到的是普通文件，名字就是 soname，正好
FFMPEG6_SONAMES="libavcodec.so.60 libavformat.so.60 libavutil.so.58 libswscale.so.7 libswresample.so.4 libavfilter.so.9 libavdevice.so.60 libpostproc.so.57"

ADB=adb
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

if [ ! -f "$FFMPEG6_LIB_DIR/libavcodec.so.60" ]; then
    echo "本地没有 FFmpeg 6.0 的库（$FFMPEG6_LIB_DIR），先运行 ./build_ffmpeg6_for_board.sh" >&2
    exit 1
fi

# md5 -q：Mac 上只输出 md5 值；板子上 md5sum 输出“值 文件名”，cut 取第一列。
# 这块板子的 adbd 不把板子上命令的退出码传回来（2026-11-18 day8 实测），所以都是比较输出的文字
LOCAL_MD5=$(md5 -q "$FFMPEG6_LIB_DIR/libavcodec.so.60")
BOARD_MD5=$($ADB shell "md5sum $BOARD_FFMPEG6_LIB_DIR/libavcodec.so.60 2>/dev/null | cut -d' ' -f1" < /dev/null | tr -d '\r')
if [ "$LOCAL_MD5" = "$BOARD_MD5" ]; then
    echo "=== 板子上的 FFmpeg 6.0 已是最新，跳过推送 ==="
    exit 0
fi

echo "=== 推送 FFmpeg 6.0 动态库到 $BOARD_FFMPEG6_LIB_DIR（带调试符号，约 130 MB，USB 要十几秒） ==="
$ADB shell "mkdir -p $BOARD_FFMPEG6_LIB_DIR" < /dev/null || exit 1
for soname in $FFMPEG6_SONAMES; do
    $ADB push "$FFMPEG6_LIB_DIR/$soname" "$BOARD_FFMPEG6_LIB_DIR/$soname" >/dev/null || exit 1
    echo "  $soname"
done
