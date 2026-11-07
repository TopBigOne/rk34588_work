//
// M3：摄像头双码流（从 main.cpp 搬过来；双码流部分和 M2 共用 DualStreamEncoder）
//
#include "app/app_config.h"
#include "app/milestones.h"
#include "camera/camera_capture.h"
#include "common/log.h"
#include "common/nv12_picture.h"
#include "common/time_utils.h"
#include "encoder/dual_stream_encoder.h"

static const char *kTag = "M3";

int run_m3_dual_stream_from_camera() {
    LOGI(kTag, "===== M3：摄像头双码流（1080p 主码流 + 720p 子码流）=====");

    // 第 1 步 开摄像头。stride 用驱动给的，不自己算
    CameraCapture camera;
    if (!camera.openAndStart(kCameraDevicePath, kMainWidth, kMainHeight)) {
        return 1;
    }

    // 第 2 步 主码流的画面就是摄像头的 buffer：宽高、stride 全用摄像头的；mppBuf 每帧换成 DQBUF 出来的那一块
    Nv12Picture mainPicture = {.mppBuf = nullptr,
                               .width = camera.getWidth(),
                               .height = camera.getHeight(),
                               .horStride = camera.getHorStride(),
                               .verStride = camera.getVerStride()};

    // 第 3 步 两个编码器的参数。和 M2 比：stride 用摄像头的；rotation = 3（摄像头侧装，Day 3 的设置）
    EncoderConfigArg mainCfg;
    mainCfg.width = mainPicture.width;
    mainCfg.height = mainPicture.height;
    mainCfg.horStride = mainPicture.horStride; // 1920
    mainCfg.verStride = mainPicture.verStride; // ⭐ 1080：填 0 的话编码器自己按 16 对齐算成 1088，UV 就找错了
    mainCfg.bps = 4 * 1000 * 1000;
    mainCfg.rotation = 3; // 编码前转 270°，码流画面是 1080 × 1920（竖的）

    // 子码流：RGA 的输出（720 是 16 的倍数，stride 就是 1280 × 720）
    const Nv12Picture subLayout = make_aligned_nv12_picture(kSubWidth, kSubHeight);
    EncoderConfigArg subCfg;
    subCfg.width = subLayout.width;
    subCfg.height = subLayout.height;
    subCfg.horStride = subLayout.horStride;
    subCfg.verStride = subLayout.verStride;
    subCfg.bps = 1500 * 1000;
    subCfg.rotation = 3; // RGA 只缩不转；转交给编码器，码流画面是 720 × 1280

    DualStreamEncoder dualStreamEncoder;
    if (!dualStreamEncoder.open(mainCfg, kCameraMainStreamPath, subCfg, kCameraSubStreamPath)) {
        return 1;
    }

    // 第 4 步 开头的帧丢掉：摄像头刚开时自动曝光、白平衡还没稳定。有一帧失败就停
    bool isAllOk = true;
    for (int frameIndex = 0; frameIndex < kSkipFrameCount && isAllOk; ++frameIndex) {
        const int filledBufIndex = camera.dequeueFilledBuffer();
        isAllOk = filledBufIndex >= 0 && camera.queueBuffer(filledBufIndex);
    }

    // 第 5 步 主循环：取 → 编主码流 + RGA 缩 → 还给摄像头 → 编子码流
    int encodedFrameCount = 0;
    const double loopStartMs = get_now_ms();
    while (isAllOk && encodedFrameCount < kRecordFrameCount) {
        // 5.1 取一块摄像头写好的 buffer（阻塞，30 fps 时最多等约 33 ms）
        const int filledBufIndex = camera.dequeueFilledBuffer();
        if (filledBufIndex < 0) {
            isAllOk = false;
            break;
        }
        mainPicture.mppBuf = camera.getMppBuffer(filledBufIndex);

        // 5.2 编码器 #1 直接读摄像头的 buffer（零拷贝）；RGA 也读它，缩进子码流 buffer
        isAllOk = dualStreamEncoder.encode_main_and_resize_to_sub(mainPicture);

        // 5.3 ⭐ 编码器 #1 和 RGA 都读完了（两个调用都是同步的），现在才能还给摄像头。
        //     子码流编码读的是 RGA 的输出，不是摄像头的 buffer，所以不用等它编完。
        //     queueBuffer 写在 && 前面：前面失败了也要还，少还一块摄像头就少一块可写的 buffer
        isAllOk = camera.queueBuffer(filledBufIndex) && isAllOk;

        // 5.4 编码器 #2 编 RGA 缩好的那一块
        isAllOk = isAllOk && dualStreamEncoder.encode_sub();
        ++encodedFrameCount;
    }
    const double loopMs = get_now_ms() - loopStartMs;

    // 第 6 步 排空两个编码器，打印结果。摄像头、编码器、文件、buffer 都在函数返回时自动释放
    if (!isAllOk || !dualStreamEncoder.drain_both()) {
        return 1;
    }
    // 实际帧率：摄像头是 30 fps，两路编码 + RGA 跟得上的话这里接近 30；跟不上就会明显低于 30
    LOGI(kTag, "%d 帧，用时 %.2f s，实际 %.1f fps", encodedFrameCount, loopMs / 1000, encodedFrameCount * 1000 / loopMs);
    LOGI(kTag, "主码流（转正后 %dx%d）：%zu 字节 -> %s", mainCfg.height, mainCfg.width,
         dualStreamEncoder.get_main_stream_bytes(), kCameraMainStreamPath);
    LOGI(kTag, "子码流（转正后 %dx%d）：%zu 字节 -> %s", subCfg.height, subCfg.width,
         dualStreamEncoder.get_sub_stream_bytes(), kCameraSubStreamPath);
    return 0;
}
