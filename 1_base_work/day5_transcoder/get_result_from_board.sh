#!/bin/sh
# 通过 USB（adb）把 M2 在板子上生成的两路码流拷到 Mac：
#   /userdata/av/main_1080p.h264（主码流）
#   /userdata/av/sub_720p.h264  （子码流）
#   → day4_rga/result_from_board/
# 只拷贝，不删板子上的文件；每个文件拷完都核对一次大小。
# 用法：./get_result_from_board.sh
# 可选环境变量：
#   ADB=/path/to/adb    不用 PATH 里的 adb 时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

# 先 cd 到脚本所在目录（day4_rga/），下面的相对路径 result_from_board 才不会随"从哪里运行脚本"而变
cd "$(dirname "$0")" || exit 1

BOARD_DIR=/userdata/av
FILE_NAMES="main_1080p.h264 sub_720p.h264"
DST_DIR=result_from_board

# adb 直接用 PATH 里的（Mac 上已经加进环境变量；Ubuntu：sudo apt install adb）
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里（Ubuntu: sudo apt install adb），或者设置 ADB=adb的路径" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

mkdir -p "$DST_DIR" || exit 1

for FILE_NAME in $FILE_NAMES; do
    SRC="$BOARD_DIR/$FILE_NAME"
    DST="$DST_DIR/$FILE_NAME"

    # 板子上的文件大小（字节）；文件不存在时 stat 失败，拿到的是空字符串
    # < /dev/null：不让 adb shell 读终端输入（否则粘贴多行命令时，后面的行会被转发到板子上，混进输出）
    # tr -dc '0-9'：只留数字
    BOARD_SIZE=$($ADB shell "stat -c %s '$SRC' 2>/dev/null" < /dev/null | tr -dc '0-9')
    if [ -z "$BOARD_SIZE" ]; then
        echo "板子上找不到 ${SRC}（先跑一次 ./run_on_board_with_mac.sh m2）" >&2
        exit 1
    fi

    echo "=== ${SRC}（${BOARD_SIZE} 字节）→ ${DST} ==="
    $ADB pull "$SRC" "$DST" || exit 1

    # 核对大小：Mac 上是 stat -f %z，Linux 上是 stat -c %s
    LOCAL_SIZE=$(stat -f %z "$DST" 2>/dev/null || stat -c %s "$DST")
    if [ "$LOCAL_SIZE" != "$BOARD_SIZE" ]; then
        echo "大小不一致：板子 ${BOARD_SIZE} 字节，Mac ${LOCAL_SIZE} 字节" >&2
        exit 1
    fi
done

echo "=== 两个码流都已拷到 $(pwd)/${DST_DIR}（板子上的文件保留） ==="
echo "播放：ffplay ${DST_DIR}/main_1080p.h264    ffplay ${DST_DIR}/sub_720p.h264"
