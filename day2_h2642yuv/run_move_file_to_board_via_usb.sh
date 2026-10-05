cd "$(dirname "$0")" || exit 1
# 默认传 aaa.264；也可以传别的文件：./run_move_file_to_board_via_usb.sh 文件路径
SRC=${1:-/Users/dev/Documents/AV/rk_test_data/aaa.264}
DST=/userdata/av/

# adb 直接用 PATH 里的（Mac 上已经加进环境变量；Ubuntu：sudo apt install adb）
ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb：请把 adb 加到 PATH 里（Ubuntu: sudo apt install adb），或者设置 ADB=adb的路径" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

if [ ! -f "$SRC" ]; then
    echo "找不到 $SRC" >&2
    exit 1
fi

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子。检查：板子开机了吗？OTG 线（TypeC0 口）插好了吗？执行 adb devices 看看" >&2
    exit 1
fi

$ADB shell "mkdir -p $DST" || exit 1
$ADB push "$SRC" "$DST" || exit 1
echo "=== 已通过 USB 传到板子 $DST ==="
