//
// M1：RGA 缩放一帧（从 main.cpp 搬过来；资源改用 RAII 自动释放）
//
#include "app/app_config.h"
#include "app/milestones.h"
#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/nv12_picture.h"
#include "common/time_utils.h"
#include "rga/rga_resizer.h"

static const char *kTag = "M1";

int run_m1_resize_one_frame() {
    LOGI(kTag, "===== M1：RGA 把一帧 1080p 缩成 720p =====");

    // 第 1 步 buffer group：DRM 类型，RGA 能通过 fd 用它；CACHABLE：CPU 读写走缓存，要配 sync
    MppBufferGroup rawGroup = nullptr;
    if (mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    MppBufferGroupPtr bufferGroup(rawGroup); // 函数返回时自动 mpp_buffer_group_put

    // 第 2 步 两块 buffer：输入 1080p（stride 1920 × 1088），输出 720p（stride 1280 × 720）
    // 声明在 bufferGroup 后面：返回时先释放 buffer，再释放 group
    Nv12Picture srcPicture = make_aligned_nv12_picture(kMainWidth, kMainHeight);
    Nv12Picture dstPicture = make_aligned_nv12_picture(kSubWidth, kSubHeight);
    MppBufferPtr srcBufOwner = alloc_nv12_buffer(bufferGroup.get(), srcPicture);
    MppBufferPtr dstBufOwner = alloc_nv12_buffer(bufferGroup.get(), dstPicture);
    FilePtr inputFile(fopen(kInputNv12FilePath, "rb"));
    FilePtr outputFile(fopen(kResizedFilePath, "wb"));
    if (!srcBufOwner || !dstBufOwner || !inputFile || !outputFile) {
        LOGE(kTag, "申请 buffer 或打开文件失败");
        return 1;
    }

    // 第 3 步 读一帧 → RGA 缩放 → 写文件
    if (!read_nv12_frame(inputFile.get(), srcPicture)) {
        LOGE(kTag, "读 %s 失败", kInputNv12FilePath);
        return 1;
    }
    const double beforeResizeMs = get_now_ms();
    const bool isResizeOk = resize_nv12_by_rga(srcPicture, dstPicture);
    const double resizeMs = get_now_ms() - beforeResizeMs;
    if (!isResizeOk || !write_nv12_frame(outputFile.get(), dstPicture)) {
        return 1;
    }

    LOGI(kTag, "%dx%d（stride %dx%d）-> %dx%d（stride %dx%d），RGA 用时 %.2f ms，写到 %s", srcPicture.width,
         srcPicture.height, srcPicture.horStride, srcPicture.verStride, dstPicture.width, dstPicture.height,
         dstPicture.horStride, dstPicture.verStride, resizeMs, kResizedFilePath);
    return 0;
}
