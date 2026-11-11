//
// Created by DEV on 2026/11/11.
//

#include "decode_stage.h"

#include <unistd.h>
#include <utility>


#include "common/log.h"

static const char *const kTag = "decodeStage";
bool DecodeStage::open(const char *streamPath, MppCodingType codingType) {
    return reader.open(streamPath) && decoder.init(codingType);
}

/**
 * jj
 * @param decodedFrame
 * @return
 */
ReadFrameResult DecodeStage::readNextFrame(MppFramePtr &decodedFrame) {
    while (true) {
        if (isOutputEos) {
            return ReadFrameResult::EndOfStream;
        }

        // step 1： 先取
        MppFramePtr outputFrame;
        auto getFrameResult = decoder.getFrame(outputFrame);
        if (getFrameResult == GetFrameResult::Error) {
            return ReadFrameResult::Error;
        }
        if (getFrameResult == GetFrameResult::Frame) {
            MppFrame rawFrame = outputFrame.get();
            if (mpp_frame_get_eos(rawFrame)) {
                isOutputEos = true;
            }

            RK_U32 errInfo = mpp_frame_get_errinfo(rawFrame);
            RK_U32 discard = mpp_frame_get_discard(rawFrame);
            if (errInfo || discard) {
                errorFrameCount++;
                LOGW(kTag, "跳过错误帧：errinfo 0x%x，discard %u", errInfo, discard);
                continue; // outputFrame 在这里析构，buffer 回到内存池
            }
            if (mpp_frame_get_buffer(rawFrame)) {
                decodedFrameCount++;
                decodedFrame = std::move(outputFrame);
                return ReadFrameResult::Frame;
            }
            continue;
        }

        // step 2: 解码器现在没有帧（None）,再送一块码流
        // 2.1 手上没后待送的数据，文件也没有读完，才读新的一块
        if (!hasPendingChunk && !isInputEos) {
            bool isLastChunk = false;
            if (!reader.readChunk(pendingChunk, isLastChunk)) {
                return ReadFrameResult::Error;
            }
            hasPendingChunk = true;
            isInputEos = isLastChunk;
            if (isInputEos) {
                LOGI(kTag, "文件读完了，最后一块带上 EOS 送进解码器");
            }
        }

        // 2.2: 送进解码器
        if (hasPendingChunk) {
            const FeedResult feedResult = decoder.feed(pendingChunk.data(), pendingChunk.size(), isInputEos);
            if (feedResult == FeedResult::Error) {
                return ReadFrameResult::Error;
            }
            if (feedResult == FeedResult::Sent) {
                hasPendingChunk = false;
                // 送进去了，马上回去读帧数据
                continue;
            }
            // todo : 真实项目中，真的可以用usleep(1000) 吗？
            usleep(1000);
            continue;
        }

        // 2.3 文件读完了，eos也送了，解码器还在解最后几帧，等1ms再取
        usleep(1000);
    }
}
