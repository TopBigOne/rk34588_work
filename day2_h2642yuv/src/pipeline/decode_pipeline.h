/*
 * DecodePipeline：解码流程的骨架 ——"读 → 送 → 取 → 写 → 统计"
 *
 *   StreamSource ──read──→ MppDecoder ──get_frame──→ FrameSink
 *                               │
 *                               └────add_xxx────→ DecodeStats
 *
 * 设计（策略模式 / 组合，和 Day 1 的 EncodePipeline 一样）：
 *   - 流程固定写在 run() 里，包括"队列满就等 1ms 重送"、"送完 EOS 还要接着取帧"这些容易写错的逻辑
 *   - 输入、输出是接口，换实现不用改这里：
 *       文件 → 文件（现在）：ReadStream + WriteYuv
 *       RTSP → 显示（以后）：新写一个 StreamSource 和一个 FrameSink
 *
 * DecodePipeline 不拥有这些对象，只保存引用 / 指针：它们的创建和释放都由 main 负责，
 * 所以 main 里它们必须比 pipeline 活得久（先声明它们，最后声明 pipeline）
 */
#pragma once

#include "decoder/mpp_decoder.h"
#include "pipeline/decode_stats.h"
#include "sink/frame_sink.h"
#include "source/stream_source.h"

class DecodePipeline {
public:
    /**
     * @param source   输入（已经 open 过）
     * @param decoder  解码器（已经 init 过）
     * @param sink     输出（已经 open 过）；传 nullptr 表示不输出（只解码，测速度）
     * @param stats    统计，run() 里往里记数据，跑完由调用方打印
     */
    DecodePipeline(StreamSource& source, MppDecoder& decoder, FrameSink* sink, DecodeStats& stats)
        : source_(source), decoder_(decoder), sink_(sink), stats_(stats) {}

    /**
     * 主循环：读一块 → 送进解码器（队列满就等 1ms 原样再送）→ 把能取的帧全取出来 → 写 / 统计
     * 结束条件：拿到带 EOS 的帧（码流解完了），或者解够 maxFrames 帧
     * @param maxFrames  最多解多少帧，0 = 解到 EOS
     * @return  成功返回 true；任何一步失败打印原因，返回 false
     */
    bool run(int maxFrames);

private:
    /**
     * 把解码器现在能给的帧全取出来（内层循环），直到它说"现在没有了"、拿到 EOS 帧、或者帧数够了
     * 结果通过 outputEos_ / reachedLimit_ 带出来
     * @return  成功返回 true；取帧或写输出失败返回 false
     */
    bool drain_frames(int maxFrames);

    StreamSource& source_;
    MppDecoder& decoder_;
    FrameSink* sink_; // 可以是 nullptr
    DecodeStats& stats_;

    bool outputEos_    = false; // 解码器吐出了带 EOS 的帧：码流全解完了
    bool reachedLimit_ = false; // 解够 maxFrames 帧了
};
