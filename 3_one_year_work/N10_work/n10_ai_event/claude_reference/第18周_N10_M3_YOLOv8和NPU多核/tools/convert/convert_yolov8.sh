#!/bin/bash
# 在 x86_64 Linux（OrbStack 的 rk-dev）上用 rknn_model_zoo v2.0.0 把 yolov8n.onnx 转成 RK3588 的 int8 .rknn。
# Mac 上执行：orb -m rk-dev bash "$(pwd)/tools/convert/convert_yolov8.sh"
# 前提：convert_yolov5.sh 跑过一次（虚拟环境 + toolkit2 2.0.0b0 装好了），这里直接用那个虚拟环境。
# 做的事：clone model_zoo 的 v2.0.0 标签 → download_model.sh 下 yolov8n.onnx → convert.py 转换
# 输出：$OUTPUT_DIR/yolov8n_rk3588.rknn
# 没跑过（2026-11-20 写）。推断的地方：v2.0.0 的 convert.py 参数顺序是 <onnx> <平台> [i8|fp] [输出路径]；
#   量化用的图片清单 ../../../datasets/COCO/coco_subset_20.txt 在仓库里自带。报错就看 examples/yolov8/README.md。
set -e

VENV_DIR=${VENV_DIR:-$HOME/rknn_toolkit2_venv}
WORK_DIR=${WORK_DIR:-$HOME/n10_convert}
MODEL_ZOO_TAG=v2.0.0  # 和 toolkit 2.0.0b0 同期；更新的标签可能要更新的 toolkit/runtime（推断）
OUTPUT_DIR=${OUTPUT_DIR:-/Users/dev/Documents/AV/rk_test_data/n10_models}
OUTPUT_MODEL_NAME=yolov8n_rk3588.rknn

if [ "$(uname -m)" != "x86_64" ]; then
    echo "要在 x86_64 Linux 上跑，现在是 $(uname -s) $(uname -m)" >&2
    exit 1
fi
if [ ! -x "$VENV_DIR/bin/python" ]; then
    echo "没有 $VENV_DIR：先跑 convert_yolov5.sh 装 toolkit2" >&2
    exit 1
fi
"$VENV_DIR/bin/python" -c "from rknn.api import RKNN; print('rknn.api 导入成功')"

# 第 1 步 取 model_zoo（只第一次 clone）
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"
if [ ! -d rknn_model_zoo ]; then
    git clone --depth 1 --branch "$MODEL_ZOO_TAG" https://github.com/airockchip/rknn_model_zoo.git
fi
cd rknn_model_zoo
git describe --tags 2>/dev/null || true

# 第 2 步 下载 ONNX（airockchip 改过输出头的 yolov8n：9 个输出，见 examples/yolov8/README.md）
cd examples/yolov8/model
if [ ! -f yolov8n.onnx ]; then
    bash download_model.sh
fi
ls -la yolov8n.onnx

# 第 3 步 转换（i8 = int8 量化）
cd ../python
"$VENV_DIR/bin/python" convert.py ../model/yolov8n.onnx rk3588 i8 "../model/$OUTPUT_MODEL_NAME"

mkdir -p "$OUTPUT_DIR"
cp "../model/$OUTPUT_MODEL_NAME" "$OUTPUT_DIR/"
ls -la "$OUTPUT_DIR"
echo "板子上跑：V8_MODEL=$OUTPUT_DIR/$OUTPUT_MODEL_NAME ./run_on_board_with_mac.sh m3cmp"
