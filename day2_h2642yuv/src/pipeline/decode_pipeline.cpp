#include "pipeline/decode_pipeline.h"

#include <cstdint> // uint8_t
#include <cstdio> // printf / perror
#include <unistd.h> // usleep
#include <vector>

/**
 * @brief 主循环：读一块 → 送进解码器 → 把能取的帧全取出来 → 必要时等 1ms
 *
 * 结束条件：拿到带 EOS 的帧（码流解完了），或者解够 maxFrames 帧。
 *
 * @param maxFrames  最多解多少帧，0 = 解到 EOS
 * @return 成功返回 true；读文件、送码流、取帧、写输出任何一步失败返回 false
 *
 * @note 【手抄】MPP 核心流程。默写时注意：什么时候读下一块；Full 时为什么【不读】下一块；
 *       usleep 的两个条件分别是什么情况
 */
bool DecodePipeline::run(int maxFrames) {
    std::vector<uint8_t> chunk; // 当前这一块码流
    bool havePending = false; // chunk 里有数据还没送进去（刚读出来，或者上次队列满没送进去）
    bool inputEos    = false; // 文件读完了：最后一块已经读出来了

    stats_.start();
    while (!outputEos_ && !reachedLimit_) {
        // step 1：手上没有待送的数据、文件也还没读完，才读新的一块
        if (!havePending && !inputEos) {
            bool isLast = false;
            if (!source_.read(chunk, isLast)) {
                return false;
            }
            havePending = true;
            inputEos    = isLast;
            if (inputEos) {
                printf("              input end, send EOS\n");
            }
        }

        // step 2：送进解码器。队列满（Full）就留着 chunk，下一轮原样再送
        if (havePending) {
            switch (decoder_.feed(chunk.data(), chunk.size(), inputEos)) {
            case FeedResult::Sent:
                havePending = false;
                break;
            case FeedResult::Full:
                stats_.add_put_retry();
                break;
            case FeedResult::Error:
                return false;
            }
        }

        // step 3：把现在能取的帧全取出来
        if (!drain_frames(maxFrames)) {
            return false;
        }

        // step 4：这块没送进去（队列满），或者 EOS 已经送了但最后一帧还没出来：等 1ms 再试
        if (havePending || (inputEos && !outputEos_ && !reachedLimit_)) {
            usleep(1000);
        }
    }
    stats_.stop();
    return true;
}

/**
 * @brief 把解码器现在能给的帧全取出来，直到它说"现在没有了"、拿到 EOS 帧、或者帧数够了
 *
 * 每一帧的判断顺序：错误帧（跳过）→ 有 buffer 的正常图像（写输出、计数）→ eos（单独判断）。
 * 结果通过成员 outputEos_ / reachedLimit_ 带出来。
 *
 * @param maxFrames  最多解多少帧，0 = 不限
 * @return 成功返回 true；取帧或写输出失败返回 false
 *
 * @note 【手抄】MPP 核心流程。默写时注意 eos 为什么放在 if / else 链外面（见 doc/decode_get_frame拿到的几种帧.md）
 */
bool DecodePipeline::drain_frames(int maxFrames) {
    while (true) {
        // 【精读】frame 声明在循环【里面】：每一圈结束时自动 mpp_frame_deinit，buffer 马上回到内存池
        //   检验：如果把它挪到 while 外面，24 块 buffer 会不会被一直占着？
        MppFramePtr frame;
        switch (decoder_.get_frame(frame)) {
        case GetResult::Frame:
            break;
        case GetResult::None:
            return true; // 解码器现在没有输出了，回去继续送码流
        case GetResult::Error:
            return false;
        }
        MppFrame rawFrame = frame.get();

        const RK_U32 errInfo = mpp_frame_get_errinfo(rawFrame);
        const RK_U32 discard = mpp_frame_get_discard(rawFrame);
        if (errInfo || discard) {
            stats_.add_error();
            printf("              frame %-4d err %x discard %x, skip\n", stats_.frame_count(), errInfo, discard);
        } else if (mpp_frame_get_buffer(rawFrame)) {
            // 正常图像（没有 buffer 的是只带 EOS 的空帧，不用写）
            if (sink_ && !sink_->write(rawFrame)) {
                perror("    fwrite in error");
                return false;
            }
            stats_.add_frame();
            if (maxFrames > 0 && stats_.frame_count() >= maxFrames) {
                reachedLimit_ = true;
            }
        }

        // EOS 单独判断：它可能贴在最后一帧图像上，也可能是一个空帧
        outputEos_ = mpp_frame_get_eos(rawFrame);
        if (outputEos_ || reachedLimit_) {
            return true;
        }
    }
}
