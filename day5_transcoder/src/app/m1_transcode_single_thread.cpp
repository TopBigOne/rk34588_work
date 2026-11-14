#include "m1_transcode_single_thread.h"

#include "app/app_config.h"
#include "app/milestones.h"
#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/time_utils.h"
#include "pipeline/decode_stage.h"
#include "pipeline/resize_encode_stage.h"
static const char *const kTag = "M1";


int runM1TranscodeSingleThread() {
    DecodeStage decodeStage;
    // step 1:
    if (!decodeStage.open(kInputH264StreamPath, MPP_VIDEO_CodingAVC)) {
        return 1;
    }
    // step 2:
    const EncoderConfig encoderConfig = {.width = kOutputWidth,
                                         .height = kOutputHeight,
                                         .codingType = MPP_VIDEO_CodingHEVC,
                                         .rcMode = MPP_ENC_RC_MODE_CBR,
                                         .bps = kOutputBps,
                                         .fps = kOutputFps,
                                         .gop = kOutputGop};

    ResizeEncodeStage resizeStage;
    if (!resizeStage.open(encoderConfig, kOutputH265StreamPath)) {
        return 1;
    }

    // step 3: 一帧，一帧的转
    const double startMs = getNowMs();
    while (true) {
        MppFramePtr decodedFramePtr;
        auto readResult = decodeStage.readNextFrame(decodedFramePtr);
        if (readResult == ReadFrameResult::EndOfStream) {
            break;
        }
        if (readResult == ReadFrameResult::Error) {
            return 1;
        }
        if (!resizeStage.resizeAndEncode(decodedFramePtr.get())) {
            return 1;
        }
    }
    // step 4: 排空编码器
    if (!resizeStage.drainEncoder()) {
        return 1;
    }
    const double elapsedMs = getNowMs() - startMs;
    // 第 5 步 打印结果
    const int encodedFrameCount = resizeStage.getEncodedFrameCount();
    LOGI(kTag, "解码 %d 帧，错误帧 %d，编码 %d 帧", decodeStage.getDecodedFrameCount(),
         decodeStage.getErrorFrameCount(), encodedFrameCount);
    LOGI(kTag, "用时 %.0f ms，%.1f fps", elapsedMs, elapsedMs > 0 ? encodedFrameCount * 1000.0 / elapsedMs : 0.0);
    LOGI(kTag, "输入 %lld 字节 → 输出 %lld 字节（%s）", static_cast<long long>(decodeStage.getInputBytes()),
         static_cast<long long>(resizeStage.getOutputBytes()), kOutputH265StreamPath);
    return 0;
}
