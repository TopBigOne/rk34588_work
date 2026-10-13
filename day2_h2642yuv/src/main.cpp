/*
 * day2_h2642yuv：用 RK3588 的 MPP 硬件解码器，把 H.264 / H.265 裸流解成 NV12 原始图像
 *
 * 整个程序只做 6 件事（对应 M1～M6）：
 *   ① 初始化解码器        MppDecoder::init       （mpp_create → mpp_init → split_parse）
 *   ② 送码流              DecodePipeline::run    （StreamSource::read → MppDecoder::feed，队列满就等 1ms 重送）
 *   ③ info change + 内存池 MppDecoder::get_frame （在类里处理掉，外面看不到）
 *   ④ 取图像、写 NV12      DecodePipeline::drain_frames（MppDecoder::get_frame → FrameSink::write）
 *   ⑤ 解到 EOS / 解够 N 帧  DecodePipeline::run
 *   ⑥ 统计 + 收拾干净       DecodeStats::print_summary；RAII 自动释放（不再有 goto CLEANUP）
 *
 * 设计（策略模式 / 组合，和 Day 1 一样）：
 *   StreamSource ──→ MppDecoder ──→ FrameSink        流程骨架在 DecodePipeline::run() 里
 *   （ReadStream）        │         （WriteYuv）       输入、输出是接口，以后换 RTSP / 显示只要新写一个实现
 *                         └──→ DecodeStats
 *
 * 文件分工（src/ 下按功能分目录，#include 都写相对 src/ 的路径）：
 *   main.cpp                       只负责"组装"：解析参数，创建各个对象，交给 DecodePipeline 跑，打印汇总
 *   app/args.*                     【解析参数】Args、usage、parse_args
 *   decoder/mpp_decoder.*          MppDecoder：只管解码器本身（含 info change 和内存池）
 *   source/stream_source.h         StreamSource 接口：输入
 *   source/read_stream.*           ReadStream : StreamSource，按 64KB 一块一块读码流文件
 *   sink/frame_sink.h              FrameSink 接口：输出
 *   sink/write_yuv.*               WriteYuv : FrameSink，按 width × height 逐行写 NV12
 *   pipeline/decode_pipeline.*     DecodePipeline：流程骨架
 *   pipeline/decode_stats.*        DecodeStats：帧数、错误帧、重试次数、用时，打印汇总
 *   common/mpp_utils.h             MPP_CHECK、RAII 句柄（和 Day 1 的同名文件一样）
 *
 * 用法：./day2_h2642yuv [-i in.h264] [-o out.nv12] [-t h264|h265] [-n 帧数]
 *   不给 -o 就不写文件（测纯解码速度）；-n 0（默认）表示解到码流结束
 * 程序带了 rpath（见 CMakeLists.txt），板子上会自动加载 /userdata/mpp_build/lib 里的 MPP 库（出厂的太旧）
 *
 * 设计文档：guide/03_重构成类.md；相关：doc/decode_get_frame拿到的几种帧.md、doc/mpp_buffer_group_limit_config.md
 *
 * 学习标记（grep -rn "【手抄】" src 就能列出来）：
 *   【手抄】  MPP 核心流程：不看原文默写到能写出来为止（Day 3、RTSP 都会反复用到）
 *            → MppDecoder::init、MppDecoder::handle_info_change、DecodePipeline::run、DecodePipeline::drain_frames
 *   【精读】  C++ 设计和资源所有权：不用抄，但要能回答旁边写的"检验"问题
 *   【扫一眼】样板代码：知道在哪里、干什么就行
 */
#include "app/args.h"
#include "decoder/mpp_decoder.h"
#include "pipeline/decode_pipeline.h"
#include "pipeline/decode_stats.h"
#include "sink/write_yuv.h"
#include "source/read_stream.h"

#include <cstdio> // printf

/**
 * @brief 程序入口：解析参数 → 创建各个对象 → DecodePipeline::run() → 打印汇总
 * @param argc  参数个数
 * @param argv  参数列表，见 usage()
 * @return 成功返回 0；参数错误或任何一步失败返回 -1（退出码 255）
 * @note 【精读】看对象的声明顺序（决定析构顺序），其余是组装代码
 */
int main(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        usage(argv[0]);
        return -1;
    }

    // 【精读】声明顺序：为什么 pipeline 最后声明，decoder 在 reader / writer 后面
    //   检验：return 时这 5 个对象按什么顺序析构？
    // ⚠️ 声明顺序 = 析构顺序的反过来（C++ 规定：局部变量按声明的相反顺序析构）
    //   - pipeline 只保存引用，所以声明在最后（最先销毁）
    //   - decoder 比 reader / writer 先销毁：先停解码器，再关文件
    ReadStream streamReader; // 输入：码流文件，析构时自动关闭
    WriteYuv yuvWriter;      // 输出：NV12 文件（没有 -o 时不 open，也不会用到）
    MppDecoder decoder;      // 析构时自动 cfg deinit → mpp_destroy → 释放内存池
    DecodeStats stats;

    // ==================== ① 初始化解码器 ====================
    if (!decoder.init(a.type)) {
        return -1;
    }
    printf("|            decoder ready (%s)\n", a.type == MPP_VIDEO_CodingAVC ? "h264" : "h265");

    // 打开输入、输出
    if (!streamReader.open(a.streamInputPath)) {
        return -1;
    }
    if (a.yuvOutputPath && !yuvWriter.open(a.yuvOutputPath)) {
        return -1;
    }

    // ==================== ②～⑤ 送码流、取图像、写文件 ====================
    DecodePipeline pipeline(streamReader, decoder, a.yuvOutputPath ? &yuvWriter : nullptr, stats);
    if (!pipeline.run(a.maxFrames)) {
        return -1;
    }

    // ==================== ⑥ 统计 + 收拾干净 ====================
    stats.print_summary(a.streamInputPath, a.yuvOutputPath, decoder.info_change_count());
    // 释放什么都不用写：return 时 pipeline、stats、decoder、yuvWriter、streamReader 按声明的相反顺序自动释放
    return 0;
}
