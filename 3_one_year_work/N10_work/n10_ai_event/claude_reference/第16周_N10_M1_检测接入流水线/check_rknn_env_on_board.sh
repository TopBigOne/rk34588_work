#!/bin/sh
# M0 第一步：查板子上的 NPU 驱动版本和 librknnrt.so 版本（计划第 2 节“未实测”的那几项）。只读，不改板子。
ADB=${ADB:-adb}
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi
$ADB get-state >/dev/null 2>&1 || { echo "adb 没找到板子" >&2; exit 1; }

$ADB shell '
echo "== NPU 驱动版本（/sys/kernel/debug/rknpu/version；没有就看 dmesg）=="
cat /sys/kernel/debug/rknpu/version 2>/dev/null || dmesg | grep -i rknpu | head -5
echo "== 板子上的 librknnrt.so =="
for libPath in /usr/lib/librknnrt.so /usr/lib64/librknnrt.so /userdata/n10/lib/librknnrt.so; do
    if [ -f "$libPath" ]; then
        echo "$libPath:"
        strings "$libPath" 2>/dev/null | grep -i "librknnrt version" | head -1
    fi
done
echo "== NPU 负载（三个核的占用）=="
cat /sys/kernel/debug/rknpu/load 2>/dev/null
' < /dev/null
