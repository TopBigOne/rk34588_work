#!/bin/sh
# 通过 USB（adb）把板子上的文件"移动"到 Mac：先 adb pull，核对大小一致后，再删掉板子上的那份。
# 用法：./run_move_file_from_board_via_usb.sh [板子上的文件] [Mac 上的目录]
#   默认：/userdata/av/bbb.yuv → ./result_from_board/
# 可选环境变量：
#   KEEP=1              只拷贝，不删板子上的文件
#   ADB=/path/to/adb    不用 PATH 里的 adb 时指定
#   ADB_SERIAL=xxxx     同时连了多台 adb 设备时，指定板子的序列号（adb devices 第一列）

cd "$(dirname "$0")" || exit 1
SRC=${1:-/userdata/av/bbb.yuv}
DST_DIR=${2:-result_from_board}
DST="$DST_DIR/$(basename "$SRC")"

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

# 板子上的文件大小（字节）；文件不存在时 stat 失败，拿到的是空字符串
BOARD_SIZE=$($ADB shell "stat -c %s '$SRC' 2>/dev/null" | tr -d '\r')
if [ -z "$BOARD_SIZE" ]; then
    echo "板子上找不到 ${SRC}" >&2
    exit 1
fi

mkdir -p "$DST_DIR" || exit 1
echo "=== 从板子拉取 ${SRC}（${BOARD_SIZE} 字节）→ ${DST} ==="
$ADB pull "$SRC" "$DST" || exit 1

# 核对大小：一致才算拷贝成功，才能删板子上的那份
LOCAL_SIZE=$(stat -f %z "$DST" 2>/dev/null || stat -c %s "$DST") # Mac 是 -f %z，Linux 是 -c %s
if [ "$LOCAL_SIZE" != "$BOARD_SIZE" ]; then
    echo "大小不一致：板子 ${BOARD_SIZE} 字节，Mac ${LOCAL_SIZE} 字节。板子上的文件保留，没有删除" >&2
    exit 1
fi

if [ "$KEEP" = "1" ]; then
    echo "=== 已拷贝到 ${DST}（KEEP=1，板子上的文件保留） ==="
else
    $ADB shell "rm -f '$SRC'" || exit 1
    echo "=== 已移动到 ${DST}，板子上的 ${SRC} 已删除 ==="
fi
