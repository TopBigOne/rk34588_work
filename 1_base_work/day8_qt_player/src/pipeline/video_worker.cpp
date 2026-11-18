#include "pipeline/video_worker.h"

#include <algorithm> // std::min
#include <utility>   // std::move
#include <vector>

#include "common/log.h"
#include "common/time_utils.h"
#include "video/rga_color_converter.h"

static const char *const kTag = "VideoWorker";

/**
 * @brief 记下要用的对象
 * @param[in] requestedVideoStream  视频流
 * @param[in] requestedPacketQueue  视频包队列
 * @param[in] requestedClock        时钟
 * @param[in] requestedState        状态
 * @param[in] requestedCallbacks    回调
 * @note 回调按值传入再 move 进成员，省一次复制
 */
VideoWorker::VideoWorker(const AVStream *requestedVideoStream, PacketQueue &requestedPacketQueue,
                         MediaClock &requestedClock, PlaybackState &requestedState,
                         VideoWorkerCallbacks requestedCallbacks) :
    videoStream(requestedVideoStream), packetQueue(requestedPacketQueue), clock(requestedClock),
    state(requestedState), callbacks(std::move(requestedCallbacks)) {}

/**
 * @brief 线程函数
 * @return 无
 * @note 第 1 步 初始化 Annex B 转换和 MPP 解码器；第 2 步 循环取包：丢旧 serial 的包、遇到新 serial 先清空解码器、
 *       结束标记就收尾、普通包就解码
 */
void VideoWorker::run() {
    LOGI(kTag, "开始");

    // 第 1 步 初始化。H.264 → MPP_VIDEO_CodingAVC，H.265 → MPP_VIDEO_CodingHEVC（AnnexbConverter 已经检查过只有这两种）
    const MppCodingType codingType =
            videoStream->codecpar->codec_id == AV_CODEC_ID_HEVC ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    if (!annexbConverter.init(videoStream) || !decoder.init(codingType)) {
        callbacks.onError("视频解码器初始化失败（只支持 H.264 / H.265）");
        // 初始化失败也要把队列里的包取走，否则解封装线程会一直卡在 push 上，音频也就停了
        QueuedPacket discardedPacket;
        while (packetQueue.pop(discardedPacket)) {
            if (discardedPacket.isEndOfStream) {
                callbacks.onFinished(discardedPacket.serial);
            }
        }
        return;
    }

    // 第 2 步 循环取包；pop 返回 false = 队列被叫停，播放器要关了
    QueuedPacket queuedPacket;
    while (packetQueue.pop(queuedPacket)) {
        if (queuedPacket.serial != state.serial) {
            continue; // seek 之前的旧包，扔掉（queuedPacket 下一轮被覆盖时释放）
        }
        if (queuedPacket.serial != currentSerial) {
            startNewSerial(queuedPacket.serial);
        }
        if (queuedPacket.isEndOfStream) {
            finishStream();
            continue;
        }
        if (!decodePacket(std::move(queuedPacket.packetPtr))) {
            callbacks.onError("视频解码出错");
        }
    }
    LOGI(kTag, "退出：显示 %d 帧，太晚丢掉 %d 帧，seek 后跳过 %d 帧，错误帧 %d，info change %d 次", presentedFrameCount,
         droppedLateFrameCount, skippedBeforeTargetFrameCount, errorFrameCount, decoder.getInfoChangeCount());
}

/**
 * @brief 开始新的 serial
 * @param[in] newSerial  新 serial
 * @return 无
 * @note 第一次（currentSerial == -1）解码器是新建的，不用 reset
 */
void VideoWorker::startNewSerial(int newSerial) {
    if (currentSerial != -1) {
        decoder.reset();         // 丢掉旧位置的参考帧和没取走的帧
        annexbConverter.flush(); // 丢掉 filter 里旧位置的包
    }
    currentSerial = newSerial;
    // serial 0 = 刚打开文件，所有帧都要；之后每次 seek，目标时间之前的帧（从关键帧解到目标之间的）都丢掉
    seekTargetUs = newSerial == 0 ? kNoPtsUs : state.seekTargetUs.load();
    isFirstFrameOfSerial = true;
    hasReachedEndOfStream = false;
}

/**
 * @brief 处理一个视频包
 * @param[in] packetPtr  视频包
 * @return true = 正常；false = 出错
 * @note 第 1 步 算 pts（微秒）；第 2 步 AVCC → Annex B；第 3 步 送进解码器（满了先取帧再重送）；第 4 步 取出能取的帧
 */
bool VideoWorker::decodePacket(AvPacketPtr packetPtr) {
    // 第 1 步 pts 换成微秒。个别文件的包没有 pts，就用 dts（解码时间）凑合
    const int64_t packetTimestamp = packetPtr->pts != AV_NOPTS_VALUE ? packetPtr->pts : packetPtr->dts;
    const int64_t ptsUs = convertToMicroseconds(packetTimestamp, videoStream->time_base);

    // 第 2 步 转成 Annex B（mp4toannexb 一般一进一出）
    std::vector<AvPacketPtr> annexbPackets;
    if (!annexbConverter.convert(std::move(packetPtr), annexbPackets)) {
        return false;
    }

    // 第 3 步 送进解码器
    for (const AvPacketPtr &annexbPacketPtr : annexbPackets) {
        while (true) {
            const FeedResult feedResult = decoder.feedPacket(annexbPacketPtr->data,
                                                             static_cast<size_t>(annexbPacketPtr->size), ptsUs);
            if (feedResult == FeedResult::Sent) {
                break;
            }
            if (feedResult == FeedResult::Error) {
                return false;
            }
            // Full：输入队列满了，先把解好的帧取走（腾出位置），再重送同一个包
            if (!drainAvailableFrames()) {
                return false;
            }
            if (shouldAbandonCurrentWork()) {
                return true; // 要停止或者 seek 了，这个包不送了
            }
            sleepForUs(1000);
        }
    }

    // 第 4 步 取出现在能取的帧
    return drainAvailableFrames();
}

/**
 * @brief 文件读完了的收尾
 * @return 无
 * @note 第 1 步 送 EOS；第 2 步 一直取帧直到 EOS 帧出来；第 3 步 报告播完了
 */
void VideoWorker::finishStream() {
    // 第 1 步 送 EOS（满了就先取帧）
    while (decoder.feedEndOfStream() == FeedResult::Full) {
        if (!drainAvailableFrames() || shouldAbandonCurrentWork()) {
            return;
        }
        sleepForUs(1000);
    }

    // 第 2 步 把剩下的帧取完。presentFrame 会把 hasReachedEndOfStream 设成 true
    int64_t lastFrameTimeUs = getSteadyTimeUs();
    while (!hasReachedEndOfStream) {
        if (shouldAbandonCurrentWork()) {
            return;
        }
        MppFramePtr framePtr;
        const GetFrameResult getResult = decoder.getFrame(framePtr);
        if (getResult == GetFrameResult::Error) {
            break;
        }
        if (getResult == GetFrameResult::None) {
            // 2 秒都没有新帧：解码器没给 EOS 帧（不应该发生），不再等了
            if (getSteadyTimeUs() - lastFrameTimeUs > 2 * 1000 * 1000) {
                LOGW(kTag, "等 EOS 帧超时");
                break;
            }
            sleepForUs(2000);
            continue;
        }
        presentFrame(framePtr.get());
        lastFrameTimeUs = getSteadyTimeUs();
    }

    // 第 3 步 报告：这个 serial 的视频播完了
    LOGI(kTag, "视频播完：显示 %d 帧，太晚丢掉 %d 帧", presentedFrameCount, droppedLateFrameCount);
    callbacks.onFinished(currentSerial);
}

/**
 * @brief 取出现在能取的帧
 * @return true = 正常；false = 出错
 * @note 每取到一帧就交给 presentFrame（可能在里面等到显示时间）
 */
bool VideoWorker::drainAvailableFrames() {
    while (true) {
        MppFramePtr framePtr;
        const GetFrameResult getResult = decoder.getFrame(framePtr);
        if (getResult == GetFrameResult::None) {
            return true;
        }
        if (getResult == GetFrameResult::Error) {
            return false;
        }
        presentFrame(framePtr.get());
    }
}

/**
 * @brief 处理一个解出来的帧
 * @param[in] frame  解码帧
 * @return 无
 * @note 第 1 步 记下 EOS 标志，没有画面的帧直接返回；第 2 步 丢掉坏帧和 seek 目标之前的帧；
 *       第 3 步 （不是这个 serial 的第一帧时）等到时钟追上 pts，晚太多就丢；第 4 步 RGA 转换并交给界面；
 *       第 5 步 没有声音时，第一帧显示完让时钟开始走
 */
void VideoWorker::presentFrame(MppFrame frame) {
    // 第 1 步 EOS 帧可能带画面也可能不带
    if (mpp_frame_get_eos(frame)) {
        hasReachedEndOfStream = true;
    }
    if (!mpp_frame_get_buffer(frame)) {
        return;
    }

    // 第 2 步 errinfo：这一帧解码出错（码流坏了）；discard：解码器建议丢掉（比如参考帧缺失）
    if (mpp_frame_get_errinfo(frame) || mpp_frame_get_discard(frame)) {
        errorFrameCount++;
        return;
    }
    const int64_t ptsUs = mpp_frame_get_pts(frame);
    if (seekTargetUs != kNoPtsUs && ptsUs < seekTargetUs) {
        skippedBeforeTargetFrameCount++; // 从关键帧解到目标时间之间的帧，只是为了解出后面的帧，不显示
        return;
    }

    // 第 3 步 等到该显示的时间。每个 serial 的第一帧不等，马上显示
    if (!isFirstFrameOfSerial) {
        while (true) {
            if (shouldAbandonCurrentWork()) {
                return;
            }
            const int64_t earlyUs = ptsUs - clock.getPositionUs(); // 正数 = 画面比时钟早，要等
            if (earlyUs <= 0) {
                break;
            }
            sleepForUs(std::min(earlyUs, kMaxWaitSliceUs)); // 最多睡 10 ms 就醒来看看，暂停、seek 时反应快
        }
        const int64_t lateUs = clock.getPositionUs() - ptsUs;
        if (lateUs > kMaxLateUs) {
            droppedLateFrameCount++; // 晚太多了，显示出来也对不上声音，丢掉去追下一帧
            return;
        }
    }

    // 第 4 步 RGA 转换加缩放，交给界面
    int outputWidth = 0;
    int outputHeight = 0;
    calculateOutputSize(static_cast<int>(mpp_frame_get_width(frame)), static_cast<int>(mpp_frame_get_height(frame)),
                        outputWidth, outputHeight);
    RgbxPicture picture;
    if (!convertNv12FrameToRgbxByRga(frame, outputWidth, outputHeight, picture)) {
        return;
    }
    callbacks.onPictureReady(std::move(picture));
    presentedFrameCount++;

    // 第 5 步 没有声音驱动时钟时，由第一帧画面让时钟开始走
    if (isFirstFrameOfSerial) {
        isFirstFrameOfSerial = false;
        if (!state.hasAudioOutput) {
            clock.startIfWaiting(currentSerial);
        }
    }
}

/**
 * @brief 算输出大小
 * @param[in]  frameWidth    画面宽
 * @param[in]  frameHeight   画面高
 * @param[out] outputWidth   输出宽
 * @param[out] outputHeight  输出高
 * @return 无
 * @note 缩放比例 = min(区域宽 / 画面宽, 区域高 / 画面高)，这样画面完整放进区域、宽高比不变，多出来的地方由界面填黑
 */
void VideoWorker::calculateOutputSize(int frameWidth, int frameHeight, int &outputWidth, int &outputHeight) const {
    const int boxWidth = state.videoBoxWidth;
    const int boxHeight = state.videoBoxHeight;
    if (boxWidth <= 0 || boxHeight <= 0) {
        outputWidth = frameWidth;
        outputHeight = frameHeight;
    } else {
        const double scale = std::min(static_cast<double>(boxWidth) / frameWidth,
                                      static_cast<double>(boxHeight) / frameHeight);
        outputWidth = static_cast<int>(frameWidth * scale);
        outputHeight = static_cast<int>(frameHeight * scale);
    }
    // 对齐：宽向下取 16 的倍数（RGA 写 RGBX 时行宽要对齐），高向下取 2 的倍数；最小 16×16
    outputWidth = std::max(16, outputWidth & ~15);
    outputHeight = std::max(16, outputHeight & ~1);
}

/**
 * @brief 要不要放弃手上的活
 * @return true = 放弃
 * @note 要求停止了，或者 serial 变了（有了新的 seek）
 */
bool VideoWorker::shouldAbandonCurrentWork() const { return state.shouldStop || state.serial != currentSerial; }
