cd "$(dirname "$0")" || exit 1
SRC=/Users/dev/Documents/AV/rk_test_data/in_1080p_60f.nv12
DST=/userdata/av/

if [ -z "$ADB" ]; then
    if command -v adb >/dev/null 2>&1; then
        ADB=adb
    elif [ -x "$HOME/Documents/Android_Env/sdk/platform-tools/adb" ]; then
        ADB="$HOME/Documents/Android_Env/sdk/platform-tools/adb"
    else
        echo "找不到 adb（Ubuntu: sudo apt install adb；或设置 ADB=adb的路径）" >&2
        exit 1
    fi
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
