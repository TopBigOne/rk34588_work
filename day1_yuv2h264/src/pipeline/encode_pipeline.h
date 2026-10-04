/*
 * EncodePipeline：编码流程的骨架 ——"读 → 编 → 写 → 统计"
 *
 *   FrameSource ──read_frame──→ MppEncoder ──encode──→ StreamSink
 *                                    │
 *                                    └──add_frame──→ EncodeStats
 *
 * 设计（策略模式 / 组合）：
 *   - 流程固定写在 run() 里，所有组合共用（包括 EOS 收尾这种容易写错的逻辑）
 *   - 输入、输出是接口，换实现不用改这里：
 *       文件 → 文件（现在）：ReadYUV + WriteStream
 *       摄像头 → 文件（Day 3）：V4L2Camera + WriteStream
 *   - 不用模板方法（继承）：输入和输出会各自变化，用继承的话每种组合都要一个子类
 *
 * EncodePipeline 不拥有这 4 个对象，只保存引用：它们的创建和释放都由 main 负责，
 * 所以 main 里它们必须比 pipeline 活得久（先声明它们，再声明 pipeline）
 */
#pragma once

#include "pipeline/encode_stats.h"
#include "source/frame_source.h"
#include "encoder/mpp_encoder.h"
#include "sink/stream_sink.h"

class EncodePipeline {
public:
    /**
     * @param source   输入（已经 open 过）
     * @param encoder  编码器（已经 init 过）
     * @param sink     输出（已经 open 过）
     * @param stats    统计，run() 里往里记数据，跑完由调用方打印
     */
    EncodePipeline(FrameSource& source, MppEncoder& encoder, StreamSink& sink, EncodeStats& stats)
        : source_(source), encoder_(encoder), sink_(sink), stats_(stats) {}

    /**
     * 跑完整个流程：
     *   ④ source.prepare（按编码器的 stride 准备输入缓冲区）
     *   ③ 写文件头（SPS/PPS）
     *   ⑤ 循环每一帧，直到编码器说"码流全给你了"
     * @return  成功返回 true；任何一步失败打印原因，返回 false
     */
    bool run();

private:
    /**
     * ③ 取 SPS/PPS，写在输出的最开头（解码器必须先拿到它们才能解码）
     * @return  成功返回 true；取不到或写不进去返回 false
     */
    bool write_header();

    /**
     * ⑤ 循环每一帧：读一帧 → 送进编码器 → 取出码流写进输出 → 记统计
     * @return  成功返回 true；编码或写输出失败返回 false
     */
    bool encode_all_frames();

    FrameSource& source_;
    MppEncoder& encoder_;
    StreamSink& sink_;
    EncodeStats& stats_;
};
