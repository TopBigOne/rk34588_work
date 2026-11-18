# 第 15 周（2027-03-01 ～ 03-07）：N10 · M0 RKNN 环境

> **所属**：Q2 · [N10 YOLO 检测 + 事件录像](../2_nvr_work/README.md) ｜ [← 第 14 周](第14周_N3_M4_循环覆盖和断电保护.md) ｜ [第 16 周 →](第16周_N10_M1_检测接入流水线.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

把 NPU 这条工具链走通一遍：在 x86 Ubuntu 上把 YOLOv5 的 ONNX 模型转成 RKNN 模型，再在板子上用它跑出检测框。

## 学完能回答的问题

1. ONNX 模型和 RKNN 模型有什么区别？模型转换是在哪台机器上做的，推理又是在哪台机器上做的？
2. int8 量化是什么？为什么量化时要给一份校准图片列表（`dataset.txt`）？
3. `rknn-toolkit2` 和 `librknnrt.so` 分别是什么，各在哪一端用？
4. 为什么官方 demo 把 YOLOv5 的后处理放在 CPU 上做，而不是放在 NPU 上？
5. 模型输入是 640×640 RGB，摄像头出的是 1920×1080 NV12，中间要做哪几步？

## 开始前要准备好

- **x86 Ubuntu**：模型转换工具只有 x86_64 Linux 的安装包，Mac 上装不了（见"资料"）。用之前编 SDK 的那台（README 第 6 节标了"待确认"，本周第一件事就是确认它能用）。
- **板子**：`adb devices` 能看到。
- **上周产出**：N3 收尾完成（本周不依赖 N3 的代码，N3 没收完也能先做本周）。
- **Claude 提前准备**（按 C 方案，待用户确认）：N10 的参考实现骨架和 `m0流程.md`，放在 `2_nvr_work/n10_ai_event/` 下。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 读资料：demo 的 `README_CN.md`；《Quick Start》PDF（只看和 RK3588 环境安装、模型转换相关的章节，章节名以 PDF 为准，没有逐页核对） | 2 h | 知道整条链路分几步、每步在哪台机器上做 |
| 2 | Ubuntu 上建 Python 虚拟环境，按版本装 `requirements_cp3x-2.0.0b0.txt` 和对应的 `.whl` | 2.5 h | `python -c "from rknn.api import RKNN"` 不报错 |
| 3 | 跑 toolkit2 自带的 `examples/onnx/yolov5/test.py`：**先把第 242 行的 `target_platform='rk3566'` 改成 `'rk3588'`**，再转换 + 在电脑上仿真推理 `bus.jpg` | 2 h | 得到 `yolov5s_relu.rknn`；仿真结果图和 `result_truth.jpg` 对得上 |
| 4 | 交叉编译 SDK 的 `rknn_yolov5_demo`（`build-linux.sh -t rk3588 -a aarch64 -b Release`），`adb push` 到板子运行，用 demo 自带的 `model/RK3588/yolov5s-640-640.rknn` 和 `model/bus.jpg` | 3 h | 板子上打印出检测框和耗时，`adb pull` 回结果图能看到框 |
| 5 | 把任务 3 自己转出来的 `.rknn` 换进去再跑一次，和自带模型的结果对比 | 1.5 h | 两个模型的检测结果、耗时记成一张表 |
| 6 | 把 demo 改成用 CLion + `rk3588-toolchain.cmake` 编译（toolchain 的 sysroot 里**没有** `rknn_api.h` 和 `librknnrt.so`，要从 SDK 的 `runtime/Linux/librknn_api/` 拿） | 2 h | N10 工程能在 Mac 上一键编译、上板运行 |
| 7 | 写一篇 `doc/RKNN工具链.md`：转换、量化、部署三步，每步的输入输出 | 2 h | 新技术点文档 |

**合计约 15 小时。**

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | YOLO 的输出是什么：3 个尺度的特征图、每个格子 3 个 anchor、每个 anchor 的 `x, y, w, h, 置信度, 80 个类别分数`。对照 `include/postprocess.h` 里的 `OBJ_CLASS_NUM`、`NMS_THRESH`（第 10 行，0.45）、`BOX_THRESH`（第 11 行，0.25） | 2.5 h |
| F | 进程和线程的区别：地址空间、切换开销、崩溃影响。写一个 `fork()` 小例子，打印父子进程的 pid 和变量地址 | 2.5 h |

## 资料

| 资料 | 位置（都已核对存在） |
|---|---|
| SDK 根目录 | `/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10`（下面简称 `SDK/`） |
| 模型转换工具 | `SDK/external/rknn-toolkit2/rknn-toolkit2/packages/`：v2.0.0-beta0，只有 `linux_x86_64` 的 `.whl`（cp36～cp311），**没有 aarch64、没有 Mac** |
| 转换示例 | `SDK/external/rknn-toolkit2/rknn-toolkit2/examples/onnx/yolov5/test.py`：第 11 行 ONNX 文件名，第 16 行 `QUANTIZE_ON = True`，第 242 行 `rknn.config(...)`，第 255 行 `rknn.build(..., dataset=DATASET)` |
| 文档 | `SDK/external/rknn-toolkit2/doc/01_Rockchip_RKNPU_Quick_Start_RKNN_SDK_V2.0.0beta0_CN.pdf`、`02_..._User_Guide_..._CN.pdf`、`04_..._API_Reference_RKNNRT_..._CN.pdf`（板端 C API） |
| 板端 demo | `SDK/external/rknpu2/examples/rknn_yolov5_demo/`：`README_CN.md`、`build-linux.sh`（第 30 行列出支持的 target，含 rk3588）、`src/main.cc`、`src/postprocess.cc` |
| 板端运行库和头文件 | `SDK/external/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so`、`.../include/rknn_api.h` |
| 板子的系统里有没有 NPU 运行库 | 正点原子默认配置 `alientek_rk3588_defconfig` 第 20 行 `#include "npu2.config"`，`rockchip/npu2.config` 里是 `BR2_PACKAGE_RKNPU2=y`。板子上实际有没有 `librknnrt.so`、版本多少，**未实测**，本周任务 4 顺便确认 |
| SDK 自带的交叉编译器 | `SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu`（给 `build-linux.sh` 的 `GCC_COMPILER` 用） |

## 验收

- [ ] Ubuntu 上 `test.py` 跑通，生成 `.rknn` 文件。
- [ ] 板子上 `rknn_yolov5_demo` 用自带模型和自己转的模型各跑一次，都输出检测框，结果图里 bus.jpg 上的人和车被框出来。
- [ ] 记下：单张推理耗时（ms）、板子上 `librknnrt.so` 的版本。
- [ ] N10 工程能用 CLion 交叉编译。

## 本周不学

- 模型训练（直接用官方的 COCO 预训练模型）。
- YOLO 的网络结构（backbone、neck 是什么）。
- 量化算法细节（只要知道"用校准图统计数值范围，把 float 映射到 int8"）。
- `rknn-toolkit-lite2`（板子上跑 Python 推理，我们用 C API）。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
