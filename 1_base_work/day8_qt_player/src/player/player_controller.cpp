#include "player/player_controller.h"

#include <QMetaObject>
#include <algorithm> // std::clamp
#include <utility>   // std::move

#include "audio/audio_output_format.h"
#include "common/log.h"

static const char *const kTag = "PlayerController";

/**
 * @brief 构造
 * @param[in] parent  Qt 父对象
 * @note 只把 parent 交给 QObject
 */
PlayerController::PlayerController(QObject *parent) : QObject(parent) {}

/**
 * @brief 析构
 * @return 无
 * @note 一定要先停线程：线程里还拿着 demuxer、clock、state 的引用，这些成员接下来就要析构了
 */
PlayerController::~PlayerController() { stop(); }

/**
 * @brief 打开文件并开始播放
 * @param[in]  filePath         文件路径
 * @param[in]  shouldPlayAudio  要不要播声音
 * @param[out] errorMessage     失败原因
 * @return true = 成功；false = 失败
 * @note 第 1 步 打开文件；第 2 步 按有哪些流建队列和工作对象；第 3 步 设初始状态；第 4 步 启动线程
 */
bool PlayerController::openFile(const QString &filePath, bool shouldPlayAudio, QString &errorMessage) {
    // 第 1 步 打开。toLocal8Bit：QString（UTF-16）转成本地编码的字节串，Linux 上就是 UTF-8，中文路径也能打开
    if (!demuxer.open(filePath.toLocal8Bit().constData())) {
        errorMessage = QStringLiteral("打不开文件：") + filePath;
        return false;
    }
    const AVStream *videoStream = demuxer.getVideoStream();
    const AVStream *audioStream = demuxer.getAudioStream();
    hasVideo = videoStream != nullptr;
    hasAudio = shouldPlayAudio && audioStream != nullptr;
    if (!hasVideo && !hasAudio) {
        errorMessage = QStringLiteral("文件里没有能播的流");
        return false;
    }

    // 第 2 步 建队列和工作对象。回调用 lambda 接到本对象的成员函数上；[this]：lambda 里能用 this
    if (hasVideo) {
        videoQueuePtr = std::make_unique<PacketQueue>(kVideoQueueCapacity);
        VideoWorkerCallbacks videoCallbacks;
        videoCallbacks.onPictureReady = [this](RgbxPicture picture) { handlePictureFromWorker(std::move(picture)); };
        videoCallbacks.onFinished = [this](int serial) { handleWorkerFinished(true, serial); };
        videoCallbacks.onError = [this](const std::string &message) { handleWorkerError(message); };
        videoWorkerPtr = std::make_unique<VideoWorker>(videoStream, *videoQueuePtr, clock, state,
                                                       std::move(videoCallbacks));
    }
    if (hasAudio) {
        audioQueuePtr = std::make_unique<PacketQueue>(kAudioQueueCapacity);
        AudioWorkerCallbacks audioCallbacks;
        audioCallbacks.onFinished = [this](int serial) { handleWorkerFinished(false, serial); };
        audioCallbacks.onError = [this](const std::string &message) { handleWorkerError(message); };
        audioWorkerPtr = std::make_unique<AudioWorker>(audioStream, *audioQueuePtr, clock, state,
                                                       std::move(audioCallbacks), getAlsaDeviceName());
    }
    demuxWorkerPtr =
            std::make_unique<DemuxWorker>(demuxer, videoQueuePtr.get(), audioQueuePtr.get(), clock, state);

    // 第 3 步 初始状态：serial 0，时钟停在 0 等第一个数据。
    // hasAudioOutput 先按"有声音"设好，音频线程打不开声卡时会自己改成 false
    state.hasAudioOutput = hasAudio;
    clock.reset(0, 0);

    // 第 4 步 启动线程。std::thread(成员函数指针, 对象指针)：新线程里执行 对象->run()
    demuxThread = std::thread(&DemuxWorker::run, demuxWorkerPtr.get());
    if (videoWorkerPtr) {
        videoThread = std::thread(&VideoWorker::run, videoWorkerPtr.get());
    }
    if (audioWorkerPtr) {
        audioThread = std::thread(&AudioWorker::run, audioWorkerPtr.get());
    }
    LOGI(kTag, "开始播放：视频 %s，声音 %s", hasVideo ? "有" : "无", hasAudio ? "有" : "无");
    return true;
}

/**
 * @brief 暂停 / 继续
 * @param[in] shouldPause  true = 暂停
 * @return 无
 * @note 先改时钟再改状态：视频线程看时钟，音频线程看状态
 */
void PlayerController::setPaused(bool shouldPause) {
    clock.setPaused(shouldPause);
    state.isPaused = shouldPause;
}

/**
 * @brief 是不是暂停着
 * @return true = 暂停着
 * @note 读 atomic
 */
bool PlayerController::isPaused() const { return state.isPaused; }

/**
 * @brief 跳到指定时间
 * @param[in] targetUs  目标时间
 * @return 无
 * @note 第 1 步 记下目标并发出请求；第 2 步 清空队列：解封装线程可能正卡在 push 上（队列满了，
 *       而视频线程暂停着不取），清空后它才能回到循环开头处理 seek
 */
void PlayerController::seekTo(int64_t targetUs) {
    const int64_t durationUs = getDurationUs();
    const int64_t clampedTargetUs = durationUs > 0 ? std::clamp<int64_t>(targetUs, 0, durationUs) : 0;
    // 第 1 步 先写目标，再写"有请求"，解封装线程看到请求时目标一定已经写好
    state.requestedSeekTargetUs = clampedTargetUs;
    state.isSeekRequested = true;
    // 第 2 步 清空队列
    if (videoQueuePtr) {
        videoQueuePtr->clear();
    }
    if (audioQueuePtr) {
        audioQueuePtr->clear();
    }
}

/**
 * @brief 现在的位置
 * @return 微秒
 * @note 时钟可能比时长多一点点（最后一帧的显示时长），限制一下
 */
int64_t PlayerController::getPositionUs() const {
    const int64_t positionUs = clock.getPositionUs();
    const int64_t durationUs = demuxer.getDurationUs();
    if (positionUs < 0) {
        return 0;
    }
    return durationUs > 0 ? std::min(positionUs, durationUs) : positionUs;
}

/**
 * @brief 时长
 * @return 微秒
 * @note 直接问 demuxer
 */
int64_t PlayerController::getDurationUs() const { return demuxer.getDurationUs(); }

/**
 * @brief 设画面区域大小
 * @param[in] width   宽
 * @param[in] height  高
 * @return 无
 * @note 写 atomic，视频线程下一帧读到
 */
void PlayerController::setVideoBoxSize(int width, int height) {
    state.videoBoxWidth = width;
    state.videoBoxHeight = height;
}

/**
 * @brief 停掉所有线程
 * @return 无
 * @note joinable()：线程启动过而且还没 join 过才是 true；对没启动的 std::thread 调用 join 会抛异常
 */
void PlayerController::stop() {
    state.shouldStop = true;
    if (videoQueuePtr) {
        videoQueuePtr->abort();
    }
    if (audioQueuePtr) {
        audioQueuePtr->abort();
    }
    std::thread *threads[] = {&demuxThread, &videoThread, &audioThread};
    for (std::thread *thread : threads) {
        if (thread->joinable()) {
            thread->join(); // 等这个线程的 run() 返回
        }
    }
    if (droppedPendingPictureCount > 0) {
        LOGI(kTag, "界面来不及显示而丢掉的画面：%d 帧", droppedPendingPictureCount.load());
    }
}

/**
 * @brief 视频线程交来一帧
 * @param[in] picture  RGBX 画面
 * @return 无
 * @note 第 1 步 界面积压太多就丢；第 2 步 包成 QImage（不复制）；第 3 步 排进 UI 线程再发信号
 */
void PlayerController::handlePictureFromWorker(RgbxPicture picture) {
    // 第 1 步 fetch_add 返回加之前的值；已经有 2 帧排着没显示，这一帧就不交了
    if (pendingPictureCount.fetch_add(1) >= kMaxPendingPictureCount) {
        pendingPictureCount.fetch_sub(1);
        droppedPendingPictureCount++;
        return;
    }

    // 第 2 步 QImage(像素地址, 宽, 高, 一行字节数, 格式, 清理函数, 清理函数的参数)：
    //   QImage 不复制像素，直接用这块内存；最后一个引用它的 QImage 析构时调用清理函数。
    //   我们 new 一个 shared_ptr 的副本当参数，让这块内存至少活到 QImage 不用为止，清理函数里 delete 这个副本。
    //   Format_RGBX8888：内存里每个像素依次是 R、G、B、X 四个字节，第 4 个字节不看（按不透明显示）。
    //   RGA 那边写的是 RK_FORMAT_RGBA_8888（排列一样，原因见 rga_color_converter.cpp），第 4 字节写什么都不影响
    using PixelBufferPtr = std::shared_ptr<std::vector<uint8_t>>;
    auto *pixelBufferHolder = new PixelBufferPtr(picture.pixelBufferPtr);
    QImage image(pixelBufferHolder->get()->data(), picture.width, picture.height, picture.strideBytes,
                 QImage::Format_RGBX8888,
                 [](void *holder) { delete static_cast<PixelBufferPtr *>(holder); }, // 不捕获变量的 lambda 能当函数指针用
                 pixelBufferHolder);

    // 第 3 步 invokeMethod(接收对象, 函数, 连接方式)：把 lambda 排进 this 所在线程（UI 线程）的事件队列，
    // 由 UI 线程执行。这样计数减 1 和发信号都在 UI 线程里发生
    QMetaObject::invokeMethod(
            this,
            [this, image]() {
                pendingPictureCount.fetch_sub(1);
                emit videoPictureReady(image); // emit 只是个空宏，写上它表示"这是在发信号"
            },
            Qt::QueuedConnection);
}

/**
 * @brief 某个线程播完了
 * @param[in] isVideo  是不是视频线程
 * @param[in] serial   播完的 serial
 * @return 无
 * @note 只有当前 serial 的"播完了"才算数；没有的那一路（或声卡打不开）当作已经播完
 */
void PlayerController::handleWorkerFinished(bool isVideo, int serial) {
    if (isVideo) {
        videoFinishedSerial = serial;
    } else {
        audioFinishedSerial = serial;
    }
    const int currentSerial = state.serial;
    const bool isVideoDone = !hasVideo || videoFinishedSerial == currentSerial;
    const bool isAudioDone = !hasAudio || !state.hasAudioOutput || audioFinishedSerial == currentSerial;
    if (serial == currentSerial && isVideoDone && isAudioDone) {
        QMetaObject::invokeMethod(this, [this]() { emit playbackFinished(); }, Qt::QueuedConnection);
    }
}

/**
 * @brief 某个线程出错
 * @param[in] message  说明
 * @return 无
 * @note 先打日志，再排进 UI 线程发信号
 */
void PlayerController::handleWorkerError(const std::string &message) {
    LOGE(kTag, "%s", message.c_str());
    const QString qtMessage = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, qtMessage]() { emit errorOccurred(qtMessage); }, Qt::QueuedConnection);
}
