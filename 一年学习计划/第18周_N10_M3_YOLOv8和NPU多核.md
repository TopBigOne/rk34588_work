# 第 18 周（2027-03-22 ～ 03-28）：N10 · M3 YOLOv8 和 NPU 多核（N10 收尾）

> **所属**：Q2 · [N10 YOLO 检测 + 事件录像](../2_nvr_work/README.md) ｜ [← 第 17 周](第17周_N10_M2_画框和事件录像.md) ｜ [第 19 周 →](第19周_N5_M1_整合架构.md) ｜ [概览](../一年学习计划.md)
> **投入**：主线约 15 小时，并行约 5 小时（20 小时是上限，不是指标）
> **细化程度**：详细
> **状态**：⬜

## 本周目标

做两组对比，用数据回答两个问题：YOLOv5 和 YOLOv8 在 RK3588 上哪个更合适？NPU 的 3 个核怎么用最快？然后做 N10 的收尾：故障注入、模拟面试、问题案例库。

## 学完能回答的问题

1. YOLOv8 和 YOLOv5 的输出有什么不同（有没有 anchor）？后处理要改哪些地方？
2. RK3588 的 NPU 有 3 个核。"一个模型同时用 3 个核"和"3 个模型实例各占 1 个核、3 个线程并行"，哪个帧率高？为什么？
3. 模型是用 toolkit2 某个版本转的，板子上的 `librknnrt.so` 是另一个版本，会出什么问题？怎么查版本？
4. 前处理的均值/方差（`mean_values`、`std_values`）设错了，检测结果会是什么样子？
5. 一路 1080p30 的视频，每帧都检测，NPU 够不够？不够的话有哪些办法？

## 开始前要准备好

- **上周产出**：画框和事件录像跑通。
- **YOLOv8 模型**：SDK 里**没有** YOLOv8 的示例（`SDK/external/rknpu2/examples/` 下只有 `rknn_yolov5_demo`）。SDK 的 demo README 指向 [rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo)，那里有没有 YOLOv8 的转换脚本和后处理代码，**未核对**，本周第一件事就是去确认。
- **x86 Ubuntu**：转 YOLOv8 模型要用（第 15 周装好的环境）。

## 主线任务

| # | 任务 | 预计 | 做完得到什么 |
|:---:|---|:---:|---|
| 1 | 确认 YOLOv8 的来源：rknn_model_zoo 里有没有 v8、要求哪个版本的 toolkit2；和板子上 `librknnrt.so` 的版本对不对得上 | 1.5 h | 一张表：模型来源、toolkit2 版本、runtime 版本 |
| 2 | 在 Ubuntu 上转出 YOLOv8 的 `.rknn`（int8 量化，`target_platform='rk3588'`） | 2 h | `yolov8n.rknn` 或同级别的模型文件 |
| 3 | 改后处理支持 v8：读懂 v8 的输出格式，写 `Yolov8PostProcess`，和 v5 的后处理用同一个接口（`detect()` 返回相同的结果结构） | 3 h | 同一段视频，v5、v8 都能出框 |
| 4 | NPU 多核对比：用 `rknn_set_core_mask` 分别设 `RKNN_NPU_CORE_0`、`RKNN_NPU_CORE_0_1_2`；再试"3 个 context、3 个线程、每个绑一个核" | 3 h | 表格：三种用法的单帧耗时、每秒能处理多少帧 |
| 5 | **故障注入**：Claude 改坏 3 处（例：输出顺序对调、`zp/scale` 用错、少调一次 `rknn_outputs_release`），你只看现象定位 | 2.5 h | 3 个问题都定位到具体行 |
| 6 | **模拟面试**：Claude 问 5 个问题（N10 范围），你先答，Claude 打分、补充 | 1.5 h | 答得不好的问题记下来 |
| 7 | **问题案例库**：N10 期间碰到的真问题至少写 2 条（现象 → 定位 → 根因 → 修复 → 依据）；README 第 8 节 N10 标完成 | 1.5 h | 案例库新增 ≥ 2 条 |

**合计约 15 小时。**

## 并行任务（E 原理和工具 / F 面试基本功）

| 线 | 任务 | 预计 |
|---|---|:---:|
| E | YOLOv5（有 anchor）和 YOLOv8（anchor-free）的输出区别，只看到"后处理怎么写"为止 | 2 h |
| F | 进程间通信之三：**消息队列**（POSIX `mq_open`）。三种 IPC（管道、共享内存、消息队列）各适合什么场景，写一页对比 | 3 h |

## 资料

| 资料 | 位置（都已核对存在） |
|---|---|
| 多核接口 | `SDK/external/rknpu2/runtime/Linux/librknn_api/include/rknn_api.h` 第 237～247 行 `rknn_core_mask` 枚举；第 546～565 行 `rknn_set_core_mask`，注释写的是 "only supported on RK3588 now" |
| v5 后处理（对照用） | `SDK/external/rknpu2/examples/rknn_yolov5_demo/src/postprocess.cc` |
| 模型来源 | demo 的 `README_CN.md` 第 5 行指向 rknn_model_zoo（GitHub，内容未核对） |
| 板端 API 文档 | `SDK/external/rknn-toolkit2/doc/04_Rockchip_RKNPU_API_Reference_RKNNRT_V2.0.0beta0_CN.pdf` |

`SDK/` = `/Users/dev/Documents/AV/openedv/Tone_rk358/atk_dlrk3588_linux5.10`。

## 验收

- [ ] 对比表 1：YOLOv5s vs YOLOv8（同一段视频）的单帧推理耗时、后处理耗时、person 的检出帧数。
- [ ] 对比表 2：单核 / 三核合用 / 三个 context 并行，每秒处理帧数。
- [ ] 结论一句话：mini NVR 里用哪个模型、哪种多核用法，理由是什么。
- [ ] 故障注入 3 个问题全部定位；模拟面试 5 题做完；问题案例库新增 ≥ 2 条。
- [ ] README 第 8 节 N10 改成 ✅，写上完成日期。

## 本周不学

- 自己训练模型、换数据集（例如只检测人的单类模型）：记进"以后再挖"。
- 模型剪枝、混合量化、精度分析工具。
- YOLOv8 的网络结构。

## 本周记录（周末填）

- 实际用时：
- 碰到的问题（写进问题案例库）：
- 下周要调整的：
