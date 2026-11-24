#include "app/m1_decode_channels.h"

#include <memory> // std::unique_ptr、std::make_unique
#include <string>
#include <thread>
#include <utility> // std::move
#include <vector>

#include "app/app_config.h"
#include "common/log.h"
#include "common/time_utils.h"
#include "pipeline/decode_stage.h"

static const char *const kTag = "M1";

/** 一路解码线程跑完以后的结果，线程写、主线程在 join 之后读 */
struct ChannelDecodeResult {
    bool isDecodeOk = false; // true = 解到 EOS；false = 出错
    double elapsedMs = 0;    // 这一路从开始解到解完用了多少毫秒
};

/**
 * @brief 一路解码线程做的事：一帧一帧解到 EOS，每帧拿到就释放，只数数
 * @param[in,out] decodeStage  这一路的解码段，只有这个线程用它
 * @param[out]    result       这一路的结果
 * @return 无（结果写在 result 里）
 * @note framePtr 声明在循环里面：每一圈结束时析构，buffer 马上回到这一路解码器的内存池
 */
static void runDecodeThread(DecodeStage &decodeStage, ChannelDecodeResult &result) {
    const double startMs = getNowMs();
    while (true) {
        MppFramePtr framePtr;
        const ReadFrameResult readFrameResult = decodeStage.readNextFrame(framePtr);
        if (readFrameResult == ReadFrameResult::EndOfStream) {
            result.isDecodeOk = true;
            break;
        }
        if (readFrameResult == ReadFrameResult::Error) {
            result.isDecodeOk = false;
            break;
        }
    }
    result.elapsedMs = getNowMs() - startMs;
}

/**
 * @brief M1：4 路解码同时跑
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 每路建一个 DecodeStage 并打开；第 2 步 每路起一个线程；第 3 步 等所有线程结束；第 4 步 打印每一路的结果
 */
int runM1DecodeChannels() {
    // 第 1 步 每路建一个 DecodeStage。DecodeStage 不能复制也不能移动（里面的 MppDecoder 删掉了复制），
    // 不能直接放进 vector（vector 扩容时要移动元素），所以 new 出来、用 unique_ptr 管，vector 里放指针
    std::vector<std::unique_ptr<DecodeStage>> decodeStagePtrs;
    for (int channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        const std::string logName = "ch" + std::to_string(channelIndex);
        std::unique_ptr<DecodeStage> decodeStagePtr = std::make_unique<DecodeStage>();
        if (!decodeStagePtr->open(kChannelInputs[channelIndex].streamPath, kChannelInputs[channelIndex].codingType,
                                  logName.c_str())) {
            return 1; // 前面已经建好的 DecodeStage 随 vector 析构，自动 mpp_destroy
        }
        decodeStagePtrs.push_back(std::move(decodeStagePtr));
    }

    // 第 2 步 每路起一个线程。results 先建好、大小固定，线程拿到的引用就不会失效
    std::vector<ChannelDecodeResult> results(kChannelCount);
    std::vector<std::thread> decodeThreads;
    const double startMs = getNowMs();
    for (int channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        DecodeStage &decodeStage = *decodeStagePtrs[channelIndex];
        ChannelDecodeResult &result = results[channelIndex];
        // emplace_back 直接在 vector 里构造 std::thread，线程马上开始跑 lambda。
        // [&decodeStage, &result]：按引用捕获这两个变量（只捕获要用的，不用 [&] 全捕获）
        decodeThreads.emplace_back([&decodeStage, &result] { runDecodeThread(decodeStage, result); });
    }

    // 第 3 步 等所有线程结束。join 之前 std::thread 不能析构，否则程序直接 terminate
    for (std::thread &decodeThread : decodeThreads) {
        decodeThread.join();
    }
    const double elapsedMs = getNowMs() - startMs;

    // 第 4 步 打印每一路的结果
    bool isAllOk = true;
    for (int channelIndex = 0; channelIndex < kChannelCount; ++channelIndex) {
        const DecodeStage &decodeStage = *decodeStagePtrs[channelIndex];
        const ChannelDecodeResult &result = results[channelIndex];
        LOGI(kTag, "ch%d %s：%s，正常帧 %d，错误帧 %d，读入 %lld 字节，用时 %.0f ms", channelIndex,
             kChannelInputs[channelIndex].streamPath, result.isDecodeOk ? "解完" : "出错",
             decodeStage.getDecodedFrameCount(), decodeStage.getErrorFrameCount(),
             static_cast<long long>(decodeStage.getInputBytes()), result.elapsedMs);
        if (!result.isDecodeOk || decodeStage.getErrorFrameCount() > 0) {
            isAllOk = false;
        }
    }
    LOGI(kTag, "4 路全部结束，总用时 %.0f ms", elapsedMs);
    return isAllOk ? 0 : 1;
}
