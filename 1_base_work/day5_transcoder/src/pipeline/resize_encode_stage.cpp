//
// Created by DEV on 2026/11/14.
//

#include "resize_encode_stage.h"
#include <common/log.h>
#include <rga/rga_resizer.h>
static const char *const kTag = "ResizeEncode";

bool ResizeEncodeStage::open(const EncoderConfig &config, const char *outputPath) {
    // step 1:
    MppBufferGroup rawGroup = nullptr;
    MPP_CHECK(mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM), false);
    bufferGroupPtr.reset(rawGroup);
    resizePicture = makeAlignedNv12Picture(config.width, config.height);
    bufferPtr = allocNv12Buffer(bufferGroupPtr.get(), resizePicture);
    if (!bufferPtr) {
        return false;
    }

    // step 2:
    if (!writer.open(outputPath)) {
        return false;
    }
    // step 3：
    EncoderConfig encoderConfig = config;
    encoderConfig.horStride = resizePicture.horStride;
    encoderConfig.verStride = resizePicture.verStride;
    if (!encoder.init(encoderConfig)) {
        return false;
    }
    // step 4: 处理码流头
    std::vector<uint8_t> header;
    if (!encoder.getHeader(header)) {
        return false;
    }
    LOGI(kTag, "码流头 %zu 字节", header.size());
    return writer.writeBytes(header);
}
bool ResizeEncodeStage::resizeAndEncode(MppFrame decodeFrame) {
    Nv12Picture decodedPicture;
    // step 1:
    if (!describeDecodedFrame(decodeFrame, decodedPicture)) {
        return false;
    }
    if (encodedFrameCount == 0) {
        LOGI(kTag, "第一帧解码画面：%dx%d，stride %dx%d", decodedPicture.width, decodedPicture.height,
             decodedPicture.horStride, decodedPicture.verStride);
    }
    // step 2:
    if (!resizeNv12ByRga(decodedPicture, resizePicture)) {
        return false;
    }
    // step 3: encode
    if (!encoder.encode(resizePicture.mppBuf, encodedPacket, false)) {
        return false;
    }
    // step 4: write to file
    if (!writer.writeBytes(encodedPacket.data)) {
        return false;
    }
    encodedFrameCount++;
    return true;
}
bool ResizeEncodeStage::drainEncoder() {
    for (int tryIndex = 0; tryIndex < 30; ++tryIndex) {
        if (!encoder.encode(nullptr,encodedPacket,true)) {
            return false;
        }
        if (!writer.writeBytes(encodedPacket.data)) {
            return false;
        }
        if (encodedPacket.isEos) {
            return true;
        }
    }

    LOGW(kTag, "送了 30 次 EOS 还没拿到 EOS 包");
    return true;

}
int ResizeEncodeStage::getEncodedFrameCount() const {
    return  encodedFrameCount;
}
