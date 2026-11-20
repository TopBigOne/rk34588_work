#!/bin/sh
# 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/run_on_board_with_mac.sh
# 编译 → adb push 程序和模型 → adb shell 运行。只走 USB。
# 用法：./run_on_board_with_mac.sh m0 [模型.rknn] [图片.jpg]
#       ./run_on_board_with_mac.sh m1 [码流.h264] [模型.rknn] [letterbox|stretch]
#       ./run_on_board_with_mac.sh m1cmp [模型.rknn] [图片.jpg]
#       ./run_on_board_with_mac.sh m2 [码流文件或 rtsp://] [模型.rknn] [文件放几遍]
#         结果：/userdata/n10/events/ch0/*.mp4、/userdata/n10/record_index.db（events 表）、/userdata/n10/m2_boxed_*.nv12
#       ./run_on_board_with_mac.sh m3bench [码流] [模型] [v5|v8]       多核用法对比（表 2）
#       ./run_on_board_with_mac.sh m3models [码流] [v5 模型] [v8 模型]  YOLOv5 vs YOLOv8（表 1）
#       ./run_on_board_with_mac.sh m3cmp [v8 模型] [图片]              v8 解码和 model_zoo 对比
#       ./run_on_board_with_mac.sh m3fault none|swap|zp|norelease     故障注入
# 环境变量：
#   BUILD_DIR=build             编译目录（默认 CLion 的 cmake-build-rk3588-debug）
#   PUSH_SDK_RKNNRT=1           板子上没有 /usr/lib/librknnrt.so 时，把 SDK 的那份 push 到 /userdata/n10/lib（rpath 会先找这里）
#   CONVERTED_MODEL=路径        额外 push 一个自己转出来的 .rknn（tools/convert/convert_yolov5.sh 的输出）
#   V8_MODEL=路径               把一个 YOLOv8 .rknn push 成 /userdata/n10/model/yolov8n.rknn（m3 的默认 v8 模型）
#                               rk-dev 转的：/Users/dev/Documents/AV/rk_test_data/n10_models/yolov8n_rk3588.rknn
#                               正点原子例程自带的是 toolkit 2.3.2 转的，2.0.0b0 的 runtime 可能加载不了（推断）
#   RKNNRT_FILE=路径            和 PUSH_SDK_RKNNRT=1 一起用：push 这一份 librknnrt.so 代替 SDK 的（比如正点原子例程里的 2.3.2）
#   STREAM_FILE=路径            把 Mac 上的测试码流 push 成 /userdata/n10/people_1080p.h264（m1 的默认输入）
#   ADB_SERIAL=xxxx             同时连了多台 adb 设备时指定板子

BIN_NAME=n10_ai_event
BUILD_DIR=${BUILD_DIR:-cmake-build-rk3588-debug}
REMOTE_DIR=/tmp/CLion/run
BOARD_MODEL_DIR=/userdata/n10/model
BOARD_LIB_DIR=/userdata/n10/lib
BOARD_MPP_LIB=/userdata/mpp_build/lib
SDK=${SDK:-/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10}
DEMO_MODEL_DIR=$SDK/external/rknpu2/examples/rknn_yolov5_demo/model
SDK_RKNNRT=${RKNNRT_FILE:-$SDK/external/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so}

cd "$(dirname "$0")" || exit 1
LOCAL_BIN="$BUILD_DIR/$BIN_NAME"

ADB=${ADB:-adb}
if ! command -v "$ADB" >/dev/null 2>&1; then
    echo "找不到 adb" >&2
    exit 1
fi
if [ -n "$ADB_SERIAL" ]; then
    ADB="$ADB -s $ADB_SERIAL"
fi

CMAKE=/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake
"$CMAKE" --build "$BUILD_DIR" --target "$BIN_NAME" || exit 1

$ADB start-server >/dev/null 2>&1
if ! $ADB get-state >/dev/null 2>&1; then
    echo "adb 没找到板子：OTG 线插 TypeC0 了吗？adb devices 看看" >&2
    exit 1
fi

# 模型、标签、测试图：只在板子上没有时 push（.rknn 8 MB，不用每次传）
$ADB shell "mkdir -p $REMOTE_DIR $BOARD_MODEL_DIR $BOARD_LIB_DIR" < /dev/null || exit 1
for modelFile in yolov5s-640-640.rknn:RK3588/yolov5s-640-640.rknn coco_80_labels_list.txt:coco_80_labels_list.txt bus.jpg:bus.jpg; do
    boardName=${modelFile%%:*}
    localPath=$DEMO_MODEL_DIR/${modelFile#*:}
    if ! $ADB shell "test -f $BOARD_MODEL_DIR/$boardName" < /dev/null; then
        echo "push $localPath → $BOARD_MODEL_DIR/$boardName"
        $ADB push "$localPath" "$BOARD_MODEL_DIR/$boardName" >/dev/null || exit 1
    fi
done
if [ -n "$CONVERTED_MODEL" ]; then
    $ADB push "$CONVERTED_MODEL" "$BOARD_MODEL_DIR/" >/dev/null || exit 1
    echo "push $CONVERTED_MODEL → $BOARD_MODEL_DIR/"
fi
if [ -n "$V8_MODEL" ]; then
    $ADB push "$V8_MODEL" "$BOARD_MODEL_DIR/yolov8n.rknn" >/dev/null || exit 1
    echo "push $V8_MODEL → $BOARD_MODEL_DIR/yolov8n.rknn"
fi
if [ -n "$STREAM_FILE" ]; then
    $ADB push "$STREAM_FILE" /userdata/n10/people_1080p.h264 >/dev/null || exit 1
    echo "push $STREAM_FILE → /userdata/n10/people_1080p.h264"
fi
if [ "$PUSH_SDK_RKNNRT" = "1" ]; then
    $ADB push "$SDK_RKNNRT" "$BOARD_LIB_DIR/" >/dev/null || exit 1
    echo "push $SDK_RKNNRT → $BOARD_LIB_DIR/（rpath 优先于 /usr/lib）"
fi

# 所有 adb shell 都加 < /dev/null：不让 adb 吃掉终端输入
$ADB push "$LOCAL_BIN" "$REMOTE_DIR/" >/dev/null || exit 1
$ADB shell "chmod +x $REMOTE_DIR/$BIN_NAME" < /dev/null || exit 1
echo "=== 板子上运行 $REMOTE_DIR/$BIN_NAME $* ==="
exec $ADB shell "LD_LIBRARY_PATH=$BOARD_MPP_LIB $REMOTE_DIR/$BIN_NAME $*" < /dev/null
