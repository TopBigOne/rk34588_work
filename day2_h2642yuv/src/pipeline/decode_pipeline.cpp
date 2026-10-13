#include "pipeline/decode_pipeline.h"

#include <cstdint> // uint8_t
#include <cstdio>  // printf / perror
#include <unistd.h> // usleep
#include <vector>

bool DecodePipeline::run(int maxFrames) {
    std::vector<uint8_t> chunk; // 当前这一块码流
    bool havePending = false;   // chunk 里有数据还没送进去（刚读出来，或者上次队列满没送进去）
    bool inputEos    = false;   // 文件读完了：最后一块已经读出来了

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

bool DecodePipeline::drain_frames(int maxFrames) {
    while (true) {
        MppFramePtr frame; // RAII：每一轮循环结束时自动 mpp_frame_deinit，buffer 回到内存池
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
