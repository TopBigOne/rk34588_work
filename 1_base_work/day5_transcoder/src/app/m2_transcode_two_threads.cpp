//
// Created by DEV on 2026/11/16.
//
// M2：双线程转码。解码线程 → FrameQueue → 缩放编码线程
//

#include "m2_transcode_two_threads.h"

#include <thread>
#include <utility> // std::move

#include "app/app_config.h"
#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/time_utils.h"
#include "pipeline/decode_stage.h"
#include "pipeline/frame_queue.h"
#include "pipeline/resize_encode_stage.h"

static const char *const kTag = "M2";

/**
 * @brief 解码线程做的事：一帧一帧解，放进队列；解完了告诉队列"没有了"
 * @param[in,out] decodeStage  解码段，只有这个线程用它
 * @param[in,out] frameQueue   放帧的队列
 * @return true = 正常解完；false = 解码出错，或者被缩放编码线程叫停
 * @note 出错时调用 frameQueue.abort()，把可能在 pop 里等着的缩放编码线程叫醒
 */
static bool runDecodeThread(DecodeStage &decodeStage, FrameQueue &frameQueue) {
    while (true) {
        MppFramePtr framePtr;
        const ReadFrameResult readFrameResult = decodeStage.readNextFrame(framePtr);
        if (readFrameResult == ReadFrameResult::EndOfStream) {
            frameQueue.closeInput(); // EOS 往下传：缩放编码线程取完剩下的帧，就会拿到 EndOfStream
            return true;
        }
        if (readFrameResult == ReadFrameResult::Error) {
            frameQueue.abort();
            return false;
        }
        // std::move：所有权交给队列。之后 framePtr 是空的，这一圈结束时不会再释放这帧
        if (!frameQueue.push(std::move(framePtr))) {
            return false; // 被缩放编码线程叫停了
        }
    }
}

/**
 * @brief 缩放编码线程做的事：从队列取帧，缩放、编码；队列说"没有了"就排空编码器
 * @param[in,out] resizeEncodeStage  缩放编码段，只有这个线程用它
 * @param[in,out] frameQueue         取帧的队列
 * @return true = 正常编完并排空；false = 缩放、编码、排空出错，或者被解码线程叫停
 * @note 出错时调用 frameQueue.abort()，把可能在 push 里等空位的解码线程叫醒
 */
static bool runResizeEncodeThread(ResizeEncodeStage &resizeEncodeStage, FrameQueue &frameQueue) {
    while (true) {
        // framePtr 声明在循环里面：这一圈结束时自动 mpp_frame_deinit，buffer 回到解码器的内存池。
        // 帧是解码线程拿到的，却在这个线程释放：MPP 的 buffer 引用计数操作内部有锁（mpp_buffer_impl.cpp 的 put_buffer），没问题
        MppFramePtr framePtr;
        const PopResult popResult = frameQueue.pop(framePtr);
        if (popResult == PopResult::EndOfStream) {
            return resizeEncodeStage.drainEncoder();
        }
        if (popResult == PopResult::Aborted) {
            return false; // 被解码线程叫停了，framePtr 是空的
        }
        if (!resizeEncodeStage.resizeAndEncode(framePtr.get())) {
            frameQueue.abort();
            return false;
        }
    }
}

/**
 * @brief M2：双线程转码
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 两个 Stage 类和 M1 一模一样，只是分到了两个线程里
 */
int runM2TranscodeTwoThreads() {
    // ⚠️ 声明顺序：decodeStage → resizeEncodeStage → frameQueue。局部变量按反顺序析构：
    //    frameQueue 最先析构，队列里剩下的帧先释放，然后才轮到 decodeStage 销毁解码器和内存池

    // 第 1 步 打开解码段
    DecodeStage decodeStage;
    if (!decodeStage.open(kInputH264StreamPath, MPP_VIDEO_CodingAVC)) {
        return 1;
    }

    // 第 2 步 打开缩放编码段：参数和 M1 一样，输出到另一个文件，方便和 M1 的结果比较
    const EncoderConfig encoderConfig = {.width = kOutputWidth,
                                         .height = kOutputHeight,
                                         .codingType = MPP_VIDEO_CodingHEVC,
                                         .rcMode = MPP_ENC_RC_MODE_CBR,
                                         .bps = kOutputBps,
                                         .fps = kOutputFps,
                                         .gop = kOutputGop};
    ResizeEncodeStage resizeEncodeStage;
    if (!resizeEncodeStage.open(encoderConfig, kM2OutputH265StreamPath)) {
        return 1;
    }

    // 第 3 步 建队列，最多排 kFrameQueueCapacity（4）帧
    FrameQueue frameQueue(kFrameQueueCapacity);

    // 第 4 步 启动两个线程，等它们都结束
    const double startMs = getNowMs();
    bool isDecodeOk = false;
    bool isResizeEncodeOk = false;
    // std::thread(函数)：创建线程，马上开始运行这个函数。
    // lambda 用 [&] 按引用捕获：线程里直接用这里的局部变量。下面 join 等线程结束，所以这些局部变量一定比线程活得久
    std::thread decodeThread([&] { isDecodeOk = runDecodeThread(decodeStage, frameQueue); });
    std::thread resizeEncodeThread([&] { isResizeEncodeOk = runResizeEncodeThread(resizeEncodeStage, frameQueue); });
    // join：等线程结束。join 返回后，线程里写的 isDecodeOk 等变量在这里一定能读到最新值
    decodeThread.join();
    resizeEncodeThread.join();
    const double elapsedMs = getNowMs() - startMs;
    if (!isDecodeOk || !isResizeEncodeOk) {
        LOGE(kTag, "失败：解码线程 %s，缩放编码线程 %s", isDecodeOk ? "正常" : "出错",
             isResizeEncodeOk ? "正常" : "出错");
        return 1;
    }

    // 第 5 步 打印结果
    const int encodedFrameCount = resizeEncodeStage.getEncodedFrameCount();
    LOGI(kTag, "解码 %d 帧，错误帧 %d，编码 %d 帧", decodeStage.getDecodedFrameCount(),
         decodeStage.getErrorFrameCount(), encodedFrameCount);
    LOGI(kTag, "用时 %.0f ms，%.1f fps", elapsedMs, elapsedMs > 0 ? encodedFrameCount * 1000.0 / elapsedMs : 0.0);
    // 最多排过的帧数等于容量，说明队列满过：解码比编码快，解码线程被背压挡住过
    LOGI(kTag, "队列容量 %zu，最多同时排过 %zu 帧", kFrameQueueCapacity, frameQueue.getMaxQueuedFrameCount());
    LOGI(kTag, "输入 %lld 字节 → 输出 %lld 字节（%s）", static_cast<long long>(decodeStage.getInputBytes()),
         static_cast<long long>(resizeEncodeStage.getOutputBytes()), kM2OutputH265StreamPath);
    return 0;
}
