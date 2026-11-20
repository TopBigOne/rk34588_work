#!/bin/bash
# 在 x86_64 Linux（OrbStack 的 rk-dev）上把 SDK 自带的 yolov5s_relu.onnx 转成 RK3588 的 int8 .rknn。
# Mac 上执行：orb -m rk-dev bash "$(pwd)/tools/convert/convert_yolov5.sh"
#   （OrbStack 把 Mac 的 /Users 挂进了 rk-dev，路径一样；推断，第一次跑时确认）
# 做的事：建 Python 虚拟环境 → 装 toolkit2 2.0.0b0 → 复制官方 examples/onnx/yolov5 → 改 target_platform → 跑 test.py
# 输出：$OUTPUT_DIR/yolov5s_relu_rk3588.rknn 和仿真结果图 result.jpg（和 result_truth.jpg 对比）
set -e

SDK=${SDK:-/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10}
TOOLKIT_DIR=$SDK/external/rknn-toolkit2/rknn-toolkit2
VENV_DIR=${VENV_DIR:-$HOME/rknn_toolkit2_venv}
WORK_DIR=${WORK_DIR:-$HOME/n10_convert/yolov5}
OUTPUT_DIR=${OUTPUT_DIR:-/Users/dev/Documents/AV/rk_test_data/n10_models}
OUTPUT_MODEL_NAME=yolov5s_relu_rk3588.rknn

if [ "$(uname -m)" != "x86_64" ]; then
    echo "要在 x86_64 Linux 上跑（rknn-toolkit2 只有 linux_x86_64 的 whl），现在是 $(uname -s) $(uname -m)" >&2
    exit 1
fi

# whl 按 Python 版本选：cp310 对应 Python 3.10（rk-dev 是 3.10.12）
PYTHON_TAG=$(python3 -c 'import sys; print(f"cp{sys.version_info.major}{sys.version_info.minor}")')
WHEEL_PATH=$(ls "$TOOLKIT_DIR"/packages/rknn_toolkit2-2.0.0b0+*-"$PYTHON_TAG"-*linux_x86_64.whl)
REQUIREMENTS_PATH=$TOOLKIT_DIR/packages/requirements_${PYTHON_TAG}-2.0.0b0.txt
echo "Python $PYTHON_TAG，whl：$WHEEL_PATH"

# 第 1 步 虚拟环境 + 安装（只第一次装；python3 -m venv 报错就先 sudo apt install python3-venv）
if [ ! -x "$VENV_DIR/bin/python" ]; then
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install --upgrade pip
    "$VENV_DIR/bin/pip" install -r "$REQUIREMENTS_PATH"
    "$VENV_DIR/bin/pip" install "$WHEEL_PATH"
fi
"$VENV_DIR/bin/python" -c "from rknn.api import RKNN; print('rknn.api 导入成功')"

# 第 2 步 复制官方示例（不改 SDK 里的原文件）
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp -r "$TOOLKIT_DIR"/examples/onnx/yolov5/. "$WORK_DIR/"
cd "$WORK_DIR"

# 第 3 步 改两处：目标芯片（第 242 行原来是 rk3566）、输出文件名
sed -i "s/target_platform='rk3566'/target_platform='rk3588'/" test.py
sed -i "s/^RKNN_MODEL = .*/RKNN_MODEL = '$OUTPUT_MODEL_NAME'/" test.py
grep -n "target_platform\|^RKNN_MODEL" test.py

# 第 4 步 转换 + 在电脑上仿真推理 bus.jpg（test.py 最后把结果图存成 result.jpg，推断，以实际输出为准）
"$VENV_DIR/bin/python" test.py

mkdir -p "$OUTPUT_DIR"
cp "$OUTPUT_MODEL_NAME" "$OUTPUT_DIR/"
for resultImage in result.jpg result_truth.jpg; do
    [ -f "$resultImage" ] && cp "$resultImage" "$OUTPUT_DIR/yolov5_$resultImage"
done
ls -la "$OUTPUT_DIR"
echo "板子上跑：CONVERTED_MODEL=$OUTPUT_DIR/$OUTPUT_MODEL_NAME ./run_on_board_with_mac.sh m0 /userdata/n10/model/$OUTPUT_MODEL_NAME"
