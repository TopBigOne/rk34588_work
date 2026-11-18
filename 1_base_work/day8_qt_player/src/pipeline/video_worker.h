#pragma once

#include <cstdint> // int64_t
#include <functional>
#include <string>

#include "common/ffmpeg_utils.h"
#include "common/mpp_utils.h"
#include "pipeline/packet_queue.h"
#include "pipeline/playback_state.h"
#include "sync/media_clock.h"
#include "video/annexb_converter.h"
#include "video/mpp_video_decoder.h"
#include "video/rgbx_picture.h"

/** 视频线程向外报告事情用的回调（在视频线程里被调用） */
struct VideoWorkerCallbacks {
    std::function<void(RgbxPicture)> onPictureReady;    // 一帧到了该显示的时间，交给界面
    std::function<void(int serial)> onFinished;         // 这个 serial 的视频播完了
    std::function<void(const std::string &)> onError;   // 出错了，参数是给人看的说明
};

/**
 * @brief 视频线程要做的事：取包 → 转 Annex B → MPP 硬解 → 等到该显示的时间 → RGA 转 RGBX → 交给界面
 * @note 显示时间看 MediaClock：画面早了就等；晚了超过 kMaxLateUs 就丢掉不显示。
 *       每个 serial（打开文件、每次 seek 之后）的第一帧不等时钟，马上显示（暂停时拖进度条也能看到新位置的画面）
 */
class VideoWorker {
public:
    /**
     * @brief 记下要用的对象
     * @param[in] requestedVideoStream  视频流（时间单位、编码格式、avcC）
     * @param[in] requestedPacketQueue  视频包队列
     * @param[in] requestedClock        播放时钟
     * @param[in] requestedState        共用状态
     * @param[in] requestedCallbacks    回调
     * @note 解码器在 run() 里创建，这样创建和使用都在视频线程里
     */
    VideoWorker(const AVStream *requestedVideoStream, PacketQueue &requestedPacketQueue, MediaClock &requestedClock,
                PlaybackState &requestedState, VideoWorkerCallbacks requestedCallbacks);

    /**
     * @brief 线程函数：初始化解码器，然后循环处理包，直到队列被叫停
     * @return 无
     * @note 在 std::thread 里运行
     */
    void run();

private:
    /**
     * @brief 开始一个新的 serial：（不是第一次时）清空解码器，记下目标时间
     * @param[in] newSerial  新的 serial
     * @return 无
     * @note serial 0 是刚打开文件，不丢任何帧；之后每次 seek，早于 seekTargetUs 的帧都丢掉
     */
    void startNewSerial(int newSerial);

    /**
     * @brief 处理一个视频包：转 Annex B，送进解码器，取出能取的帧
     * @param[in] packetPtr  视频包（AVCC），所有权交进来
     * @return true = 正常；false = 出错
     * @note 解码器输入满了时，先取帧再重送
     */
    bool decodePacket(AvPacketPtr packetPtr);

    /**
     * @brief 文件读完了：告诉解码器 EOS，把剩下的帧都取出来显示完，然后报告"播完了"
     * @return 无
     * @note 取到带 EOS 标志的帧、或者 2 秒都没有新帧，就算结束
     */
    void finishStream();

    /**
     * @brief 把解码器里现在能取的帧都取出来处理
     * @return true = 正常；false = 出错
     * @note 取到 None 就返回
     */
    bool drainAvailableFrames();

    /**
     * @brief 处理一个解出来的帧：丢掉坏帧和 seek 目标之前的帧，等到该显示的时间，转换并交给界面
     * @param[in] frame  解码帧
     * @return 无
     * @note 等待期间如果要求停止、或者有了新的 seek，就直接返回不显示
     */
    void presentFrame(MppFrame frame);

    /**
     * @brief 按画面区域的大小，算出保持宽高比的输出大小
     * @param[in]  frameWidth    画面宽
     * @param[in]  frameHeight   画面高
     * @param[out] outputWidth   输出宽（16 的倍数）
     * @param[out] outputHeight  输出高（2 的倍数）
     * @return 无
     * @note 画面区域还不知道多大时，按原尺寸输出
     */
    void calculateOutputSize(int frameWidth, int frameHeight, int &outputWidth, int &outputHeight) const;

    /**
     * @brief 现在要不要放弃手上的活：要求停止了，或者有了新的 seek
     * @return true = 放弃
     * @note 等待、重试的循环里都要检查
     */
    bool shouldAbandonCurrentWork() const;

    const AVStream *videoStream;     // 视频流
    PacketQueue &packetQueue;        // 视频包队列
    MediaClock &clock;               // 播放时钟
    PlaybackState &state;            // 共用状态
    VideoWorkerCallbacks callbacks;  // 回调
    AnnexbConverter annexbConverter; // AVCC → Annex B
    MppVideoDecoder decoder;         // MPP 硬解

    int currentSerial = -1;               // 正在处理的 serial；-1 = 还没开始
    int64_t seekTargetUs = kNoPtsUs;      // 早于它的帧丢掉；kNoPtsUs = 不丢
    bool isFirstFrameOfSerial = true;     // 这个 serial 还没显示过帧
    bool hasReachedEndOfStream = false;   // 取到了带 EOS 标志的帧

    int presentedFrameCount = 0;          // 显示了多少帧
    int droppedLateFrameCount = 0;        // 太晚了丢掉的帧数
    int skippedBeforeTargetFrameCount = 0; // seek 后早于目标时间丢掉的帧数
    int errorFrameCount = 0;              // 带错误标志的帧数

    static constexpr int64_t kMaxLateUs = 100 * 1000;        // 晚了超过 100 ms 就丢掉
    static constexpr int64_t kMaxWaitSliceUs = 10 * 1000;    // 等待时每次最多睡 10 ms，好及时发现停止 / seek
};
