//
// Created by DEV on 2026/11/14.
//
#pragma once

#include <cstdint>

#include "common/mpp_utils.h"
#include "common/nv12_picture.h"
#include "encoder/mpp_encoder.h"
#include "sink/stream_file_writer.h"

class ResizeEncodeStage {

public:
    bool open(const EncoderConfig &config, const char *outputPath);

    bool resizeAndEncode(MppFrame decodeFrame);

    /**
     * 排空编码器
     * @return
     */
    bool drainEncoder();

    int getEncodedFrameCount() const;

    /**
    * @brief 输出文件一共写了多少字节（包括码流头）
    * @return 字节数
    */
    int64_t getOutputBytes() const { return writer.getWritenBytes(); }


private:
    MppBufferGroupPtr bufferGroupPtr;
    MppBufferPtr bufferPtr;
    Nv12Picture resizePicture;
    StreamFileWriter writer;
    MppEncoder encoder;
    EncodedPacket encodedPacket;
    int encodedFrameCount = 0;
};
