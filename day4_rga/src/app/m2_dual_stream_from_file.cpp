//
// M2：文件双码流（从 main.cpp 搬过来；两个编码器、子码流 buffer、两个文件收进了 DualStreamEncoder）
//
#include "app/app_config.h"
#include "app/milestones.h"
#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/nv12_picture.h"
#include "encoder/dual_stream_encoder.h"

static const char *kTag = "M2";

int run_m2_dual_stream_from_file() {
    LOGI(kTag, "===== M2：文件双码流（1080p 主码流 + 720p 子码流）=====");

    // 第 1 步 输入画面的 buffer：自己申请，stride 1920 × 1088
    MppBufferGroup rawGroup = nullptr;
    if (mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    MppBufferGroupPtr bufferGroup(rawGroup);
    Nv12Picture mainPicture = make_aligned_nv12_picture(kMainWidth, kMainHeight);
    MppBufferPtr mainBufOwner = alloc_nv12_buffer(bufferGroup.get(), mainPicture);
    FilePtr inputFile(fopen(kInputNv12FilePath, "rb"));
    if (!mainBufOwner || !inputFile) {
        LOGE(kTag, "申请 buffer 或打开 %s 失败", kInputNv12FilePath);
        return 1;
    }

    // 第 2 步 两路编码参数：只差宽高、stride、码率三项
    EncoderConfigArg mainCfg;
    mainCfg.width = mainPicture.width;
    mainCfg.height = mainPicture.height;
    mainCfg.horStride = mainPicture.horStride;
    mainCfg.verStride = mainPicture.verStride;
    mainCfg.bps = 4 * 1000 * 1000;

    const Nv12Picture subLayout = make_aligned_nv12_picture(kSubWidth, kSubHeight); // 只用它算出来的 stride
    EncoderConfigArg subCfg;
    subCfg.width = subLayout.width;
    subCfg.height = subLayout.height;
    subCfg.horStride = subLayout.horStride;
    subCfg.verStride = subLayout.verStride;
    subCfg.bps = 1500 * 1000;

    DualStreamEncoder dualStreamEncoder;
    if (!dualStreamEncoder.open(mainCfg, kMainH264StreamPath, subCfg, kSubH264StreamPath)) {
        return 1;
    }

    // 第 3 步 每一帧：读进 mainPicture → 编主码流 + RGA 缩 → 编子码流
    int frameCount = 0;
    bool isAllOk = true;
    while (isAllOk && read_nv12_frame(inputFile.get(), mainPicture)) {
        isAllOk = dualStreamEncoder.encode_main_and_resize_to_sub(mainPicture) && dualStreamEncoder.encode_sub();
        ++frameCount;
    }

    // 第 4 步 排空两个编码器，打印结果
    if (!isAllOk || !dualStreamEncoder.drain_both()) {
        return 1;
    }
    LOGI(kTag, "%d 帧；RGA 缩放平均 %.2f ms/帧", frameCount,
         frameCount > 0 ? dualStreamEncoder.get_resize_total_ms() / frameCount : 0);
    LOGI(kTag, "主码流 %dx%d：%zu 字节 -> %s", mainCfg.width, mainCfg.height, dualStreamEncoder.get_main_stream_bytes(),
         kMainH264StreamPath);
    LOGI(kTag, "子码流 %dx%d：%zu 字节 -> %s", subCfg.width, subCfg.height, dualStreamEncoder.get_sub_stream_bytes(),
         kSubH264StreamPath);
    return 0;
}
