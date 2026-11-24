#include "pipeline/decode_stage.h"

#include <unistd.h> // usleep
#include <utility>  // std::move

#include "common/log.h"

static const char *const kTag = "DecodeStage";

/**
 * @brief 打开码流文件，初始化解码器
 * @param[in] streamPath        码流文件路径
 * @param[in] codingType        码流格式
 * @param[in] requestedLogName  日志名字
 * @return true = 成功；false = 失败
 * @note && 是短路的：文件打不开就不会去创建解码器
 */
bool DecodeStage::open(const char *streamPath, MppCodingType codingType, const char *requestedLogName) {
    logName = requestedLogName;
    return reader.open(streamPath) && decoder.init(codingType, requestedLogName);
}

/**
 * @brief 交出下一帧正常画面
 * @param[out] decodedFramePtr  解出的帧
 * @return Frame / EndOfStream / Error
 * @note 每一圈循环：第 1 步 先取帧（有就判断、交出去）；第 2 步 没有帧就读一块码流；第 3 步 把这块送进解码器
 */
ReadFrameResult DecodeStage::readNextFrame(MppFramePtr &decodedFramePtr) {
    while (true) {
        if (isOutputEos) {
            return ReadFrameResult::EndOfStream;
        }

        // 第 1 步 先取帧
        MppFramePtr outputFramePtr;
        const GetFrameResult getFrameResult = decoder.getFrame(outputFramePtr);
        if (getFrameResult == GetFrameResult::Error) {
            return ReadFrameResult::Error;
        }
        if (getFrameResult == GetFrameResult::Frame) {
            MppFrame rawFrame = outputFramePtr.get();
            // EOS 可能贴在最后一帧画面上（有 buffer），也可能是单独的空帧（没 buffer）。
            // 先记下来，这一帧有画面就照样交出去，下一次调用时再返回 EndOfStream
            if (mpp_frame_get_eos(rawFrame)) {
                isOutputEos = true;
            }
            // errinfo：这帧解码出错；discard：解码器建议丢掉。两个都不为 0 才是好帧
            const RK_U32 errorInfo = mpp_frame_get_errinfo(rawFrame);
            const RK_U32 discardFlag = mpp_frame_get_discard(rawFrame);
            if (errorInfo || discardFlag) {
                errorFrameCount++;
                LOGW(kTag, "[%s] 跳过错误帧：errinfo 0x%x，discard %u", logName.c_str(), errorInfo, discardFlag);
                continue; // outputFramePtr 在这里析构，buffer 回到解码器内存池
            }
            if (mpp_frame_get_buffer(rawFrame)) {
                if (ptsFrameRate > 0) {
                    // 第几帧（从 0 开始）× 每帧多少微秒。先乘后除，用 int64_t：3600 × 1000000 超过 int 的范围
                    const int64_t ptsUs = static_cast<int64_t>(decodedFrameCount) * 1000000 / ptsFrameRate;
                    mpp_frame_set_pts(rawFrame, ptsUs); // 写进帧里，拼图线程用 mpp_frame_get_pts 读
                }
                decodedFrameCount++;
                decodedFramePtr = std::move(outputFramePtr);
                return ReadFrameResult::Frame;
            }
            continue; // 没有 buffer 的帧（只带 EOS 的空帧）：丢掉，回到循环开头返回 EndOfStream
        }

        // 第 2 步 没有帧：手里没有待送的码流，就从文件读一块
        if (!hasPendingChunk && !isInputEos) {
            bool isLastChunk = false;
            if (!reader.readChunk(pendingChunk, isLastChunk)) {
                return ReadFrameResult::Error;
            }
            hasPendingChunk = true;
            isInputEos = isLastChunk;
        }

        // 第 3 步 送进解码器。Full：输入队列满了，先睡 1 ms 让解码器消化，下一圈先取帧再重送同一块
        if (hasPendingChunk) {
            const FeedResult feedResult = decoder.feed(pendingChunk.data(), pendingChunk.size(), isInputEos);
            if (feedResult == FeedResult::Error) {
                return ReadFrameResult::Error;
            }
            if (feedResult == FeedResult::Sent) {
                hasPendingChunk = false;
                continue;
            }
        }
        usleep(1000); // 1000 微秒 = 1 ms。码流都送完了、帧还没出来时，也在这里等
    }
}
