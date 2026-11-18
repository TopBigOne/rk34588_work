//
// Created by DEV on 2026/11/11.
//

#pragma once
#include <cstdint>
#include <vector>
#include "common/mpp_utils.h"
#include "decoder/mpp_decoder.h"
#include "source/stream_file_reader.h"

/**
 * readNextFrame()的结果
 */
enum class ReadFrameResult { Frame, EndOfStream, Error };


class DecodeStage {

public:
    /**
     * 打开码流文件，初始化解码器
     * @param streamPath
     * @param codingType
     * @return
     */
    bool open(const char *streamPath, MppCodingType codingType);

    /**
     * 交出下一帧正常画面
     * @param decodedFrame
     * @return
     */
    ReadFrameResult readNextFrame(MppFramePtr &decodedFrame);

    int getDecodedFrameCount() const { return decodedFrameCount; }

    int getErrorFrameCount() const { return errorFrameCount; }

    int64_t getInputBytes() const { return reader.getReadBytes(); }

private:
    StreamFileReader reader;
    MppDecoder decoder;
    std::vector<uint8_t> pendingChunk;
    bool hasPendingChunk= false;
    bool isInputEos = false;
    bool isOutputEos = false;
    int decodedFrameCount = 0;
    int errorFrameCount = 0;
};
