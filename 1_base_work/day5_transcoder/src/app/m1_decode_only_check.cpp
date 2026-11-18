//
// Created by DEV on 2026/11/11.
//
#include "app/app_config.h"
#include "app/milestones.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "pipeline/decode_stage.h"

static const char *const kTag = "M1DecodeCheck";


int runM1DecodeOnlyCheck() {
    DecodeStage decodeStage;
    bool open = decodeStage.open(kInputH264StreamPath, MPP_VIDEO_CodingAVC);
    if (!open) {
        LOGE(kTag, "打开 %s 或初始化解码器失败", kInputH264StreamPath);
        return 1;
    }
    LOGI(kTag, "开始解码 %s", kInputH264StreamPath);
    const double startMs = getNowMs();

    while (true) {
        MppFramePtr decodeFramePtr;
        auto readFrameResult = decodeStage.readNextFrame(decodeFramePtr);
        if (readFrameResult == ReadFrameResult::Error) {
            LOGE(kTag, "    解码出错，已经解出 %d 帧", decodeStage.getDecodedFrameCount());
            return 1;
        }
        if (readFrameResult == ReadFrameResult::EndOfStream) {
            LOGE(kTag, "    读到数据流尾部了");
            break;
        }

        int decodedFrameCount = decodeStage.getDecodedFrameCount();
        if (decodedFrameCount == 1) {
            auto firstFrame = decodeFramePtr.get();
            LOGI(kTag, "    第 1 帧：%ux%u，stride %ux%u，fmt 0x%x", mpp_frame_get_width(firstFrame),
                 mpp_frame_get_height(firstFrame), mpp_frame_get_hor_stride(firstFrame),
                 mpp_frame_get_ver_stride(firstFrame), static_cast<unsigned>(mpp_frame_get_fmt(firstFrame)));
        }
        // 每 300 帧打一行进度，免得终端半天没输出、以为卡住了
        if (decodedFrameCount % 300 == 0) {
            LOGI(kTag, "已解出 %d 帧", decodedFrameCount);
        }
    }
    // 第 3 步：打印结果，对照验收标准
    const double elapsedMs = getNowMs() - startMs;
    LOGI(kTag, "解码完成：正常帧 %d，错误帧 %d，读入 %lld 字节，用时 %.0f ms", decodeStage.getDecodedFrameCount(),
         decodeStage.getErrorFrameCount(), static_cast<long long>(decodeStage.getInputBytes()), elapsedMs);
    return decodeStage.getErrorFrameCount() == 0 ? 0 : 1;
}
