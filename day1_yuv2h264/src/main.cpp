/*
 * day1_yuv2h264：用 RK3588 的 MPP 硬件编码器，把 NV12 原始图像文件编码成 H.264 / H.265 裸流
 *
 * 整个程序只做 6 件事（和 readme.md 第 2 节对应）：
 *   ① 开一台编码机        MppEncoder::init  （mpp_create → mpp_init）
 *   ② 告诉它要什么效果    MppEncoder::init  （mpp_enc_cfg_init → GET_CFG → CFG_SET × N → SET_CFG）
 *   ③ 写文件头            EncodePipeline::write_header：MppEncoder::get_header（GET_HDR_SYNC）→ StreamSink::write
 *   ④ 准备硬件内存        FrameSource::prepare（ReadYUV：mpp_buffer_group_get_internal → mpp_buffer_get）
 *   ⑤ 循环每一帧          EncodePipeline::encode_all_frames：FrameSource → MppEncoder → StreamSink → EncodeStats
 *   ⑥ 收拾干净            RAII：MppEncoder 的析构函数 + common/mpp_utils.h 里的 XxxPtr 句柄，离开作用域自动释放
 *
 * 设计（策略模式 / 组合）：
 *   FrameSource ──→ MppEncoder ──→ StreamSink      流程骨架在 EncodePipeline::run() 里
 *   （ReadYUV）          │          （WriteStream）    输入、输出是接口，Day 3 换摄像头只要新写一个 FrameSource
 *                        └──→ EncodeStats
 *
 * 文件分工（src/ 下按功能分目录，#include 都写相对 src/ 的路径）：
 *   main.cpp                       只负责"组装"：解析参数，创建各个对象，交给 EncodePipeline 跑，打印汇总
 *   app/args.*                     【解析参数】Args、usage、parse_args、to_encoder_config
 *   encoder/mpp_encoder.*          MppEncoder 类：只管编码器本身（设计见 guide/03 第 3 节）
 *   source/frame_source.h          FrameSource 接口：输入
 *   source/read_yuv.*              ReadYUV : FrameSource，打开 NV12 文件，申请 DRM 缓冲区，按 stride 一帧一帧读进去
 *   sink/stream_sink.h             StreamSink 接口：输出
 *   sink/write_stream.*            WriteStream : StreamSink，创建输出文件，写文件头和每一帧码流
 *   pipeline/encode_pipeline.*     EncodePipeline：流程骨架（准备缓冲区 → 写文件头 → 读 → 编 → 写 → 统计）
 *   pipeline/encode_stats.*        EncodeStats 统计工具类：帧数、I 帧数、字节数、用时，打印汇总
 *   common/mpp_utils.h             ALIGN、MPP_CHECK、RAII 句柄
 *
 * 用法：./day1_yuv2h264 -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]
 *                       [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]
 * 程序带了 rpath（见 CMakeLists.txt），板子上会自动加载 /userdata/mpp_build/lib 里的 MPP 库（出厂的太旧）
 *
 * 相关文档：guide/06（本文件用到的 MPP 函数）、guide/07（MPP 数据类型）、
 *          nv12中yuv分布效果图和读取方式.md、MppPacket和MppFrame的用法和区别.md
 */
#include "app/args.h"
#include "encoder/mpp_encoder.h"
#include "pipeline/encode_pipeline.h"
#include "pipeline/encode_stats.h"
#include "sink/write_stream.h"
#include "source/read_yuv.h"
#include <cstdio> // printf

int main(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        usage(argv[0]);
        return -1;
    }

    // ⚠️ 声明顺序 = 析构顺序的反过来（C++ 规定：局部变量按声明的相反顺序析构）
    // 任何一步 return，析构顺序都是：编码器 → 输出文件 → 输入（缓冲区 → 内存池 → 文件）
    //   - 编码器最先销毁，和原来 CLEANUP 里的顺序一样
    //   - pipeline 只保存引用，所以要声明在它们后面（比它们先销毁）
    ReadYUV nv12Reader;       // 输入：NV12 文件 + DRM 缓冲区，析构时自动释放
    WriteStream streamWriter; // 输出：H.264 / H.265 裸流文件，析构时自动关闭
    MppEncoder encoder;
    EncodeStats stats;

    // ==================== ① 开一台编码机 + ② 告诉它要什么效果 ====================
    if (!encoder.init(to_encoder_config(a))) {
        return -1;
    }
    printf("|            encoder ready: %dx%d stride %dx%d\n", a.width, a.height, encoder.hor_stride(),
        encoder.ver_stride());

    // 打开输入、输出
    if (!streamWriter.open(a.streamOutputPath)) {
        return -1;
    }
    if (!nv12Reader.open(a.nv12InputPath, a.width, a.height)) {
        return -1;
    }

    // ==================== ③ 写文件头 + ④ 准备硬件内存 + ⑤ 循环每一帧 ====================
    EncodePipeline pipeline(nv12Reader, encoder, streamWriter, stats);
    if (!pipeline.run()) {
        return -1;
    }
    stats.print_summary(a.streamOutputPath, a.type, a.fps, a.bps);

    // ==================== ⑥ 收拾干净 ====================
    // 什么都不用写：return 时 pipeline、encoder、streamWriter、nv12Reader 按声明的相反顺序自动释放
    return 0;
}
