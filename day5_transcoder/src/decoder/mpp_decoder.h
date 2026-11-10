//
// Created by DEV on 2026/11/10.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <rockchip/rk_mpi.h>
#include "common/mpp_utils.h"
/**
 * feed() 的结果
 */
enum class FeedResult {
    Sent,
    Full,
    Error,
};
enum class GetFrameResult {
    Frame,
    None,
    Error,
};


class MppDecoder {
public:
    MppDecoder() = default;
    ~MppDecoder();
    MppDecoder(const MppDecoder &) = delete;
    MppDecoder &operator=(const MppDecoder &) = delete;

    /**
     * 创建解码器，并打开内部分帧
     * @param codingType
     * @return
     */
    bool init(MppCodingType codingType);

    /**
     * 送一块码流
     * @param data
     * @param sizeBytes
     * @param isEos
     * @return
     */
    FeedResult feed(const uint8_t *data, size_t sizeBytes, bool isEos);

    /**
     * 取一个输出帧
     * @param outputFrame
     * @return
     */
    GetFrameResult getFrame(MppFramePtr &outputFrame);

    int getInfoChangeCount() const { return infoChangeCount; }

private:
    bool handleInfoChange(MppFrame infoChangeFrame);
    MppBufferGroupPtr frameGroupPtr;
    MppCtx decoderCtx = nullptr;
    MppApi *decoderApi = nullptr;
    MppDecCfg decoderCfg = nullptr;
    int infoChangeCount = 0;
    static constexpr int kFrameBufferCount = 24;
};
