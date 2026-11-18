#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <atomic>
#include <cstdint> // int64_t
#include <memory>  // std::unique_ptr
#include <thread>

#include "demux/mp4_demuxer.h"
#include "pipeline/audio_worker.h"
#include "pipeline/demux_worker.h"
#include "pipeline/packet_queue.h"
#include "pipeline/playback_state.h"
#include "pipeline/video_worker.h"
#include "player/desktop_volume_watcher.h"
#include "sync/media_clock.h"
#include "video/rgbx_picture.h"

/**
 * @brief 播放器的总控：拥有拆封装、两个队列、时钟和三个工作线程；对界面提供打开、暂停、seek、查进度
 * @note 这个对象住在 UI 线程（在 UI 线程里创建），公开函数都只在 UI 线程调用。
 *       工作线程通过回调把画面、"播完了"、错误交过来，这里转成 Qt 信号：
 *       信号连到 UI 线程的对象时，Qt 自动把调用排进 UI 线程的事件队列（排队连接），界面代码不用管多线程
 */
class PlayerController : public QObject {
    // Q_OBJECT：让 moc 给这个类生成信号槽需要的代码（moc_player_controller.cpp），有信号的类必须写
    Q_OBJECT

public:
    /**
     * @brief 构造，还不打开文件
     * @param[in] parent  Qt 的父对象；父对象析构时会自动 delete 这个对象
     * @note explicit：不允许从 QObject* 隐式转换成 PlayerController
     */
    explicit PlayerController(QObject *parent = nullptr);

    /**
     * @brief 析构：停掉所有线程
     * @return 无
     * @note override：明确表示覆盖了 QObject 的虚析构函数
     */
    ~PlayerController() override;

    /**
     * @brief 打开文件并开始播放
     * @param[in]  filePath         mp4 文件路径
     * @param[in]  shouldPlayAudio  false = 只播画面（M2 用）
     * @param[out] errorMessage     失败时写入原因
     * @return true = 成功开始播放；false = 打开失败
     * @note 只能调用一次。成功后三个线程（有的流才建）开始运行
     */
    bool openFile(const QString &filePath, bool shouldPlayAudio, QString &errorMessage);

    /**
     * @brief 暂停 / 继续
     * @param[in] shouldPause  true = 暂停
     * @return 无
     * @note 时钟停住，视频线程自然停在"等时钟"那一步；音频线程看到 isPaused 让声卡暂停
     */
    void setPaused(bool shouldPause);

    /**
     * @brief 现在是不是暂停着
     * @return true = 暂停着
     * @note 读共用状态
     */
    bool isPaused() const;

    /**
     * @brief 跳到指定时间
     * @param[in] targetUs  目标时间，微秒；超出范围会被限制到 [0, 时长]
     * @return 无
     * @note 只是发出请求，由解封装线程处理。连续调用多次时，只有最后一次的目标有效
     */
    void seekTo(int64_t targetUs);

    /**
     * @brief 现在播到哪里了
     * @return 微秒
     * @note 就是 MediaClock 的位置
     */
    int64_t getPositionUs() const;

    /**
     * @brief 文件时长
     * @return 微秒；不知道时是 0
     * @note 打开文件后才有意义
     */
    int64_t getDurationUs() const;

    /**
     * @brief 告诉视频线程画面区域多大，RGA 按这个大小缩放
     * @param[in] width   宽，像素
     * @param[in] height  高，像素
     * @return 无
     * @note 界面大小变了时调用；下一帧开始生效
     */
    void setVideoBoxSize(int width, int height);

    /**
     * @brief 停掉所有线程（可以重复调用）
     * @return 无
     * @note 第 1 步 设 shouldStop；第 2 步 叫停两个队列，把在 push / pop 里等的线程叫醒；第 3 步 等三个线程退出；
     *       第 4 步 释放音频线程的对象：它拿着打开的声卡，释放了声卡才关（不然要等本对象析构才关）
     */
    void stop();

signals:
    /**
     * @brief 有一帧画面该显示了
     * @param[in] picture  画面（像素没有复制，QImage 引用着视频线程申请的那块内存）
     * @note 在 UI 线程里发出
     */
    void videoPictureReady(const QImage &picture);

    /**
     * @brief 画面和声音都播完了
     * @note 在 UI 线程里发出；可能连着发两次，接收方按"已经结束"处理即可
     */
    void playbackFinished();

    /**
     * @brief 出错了
     * @param[in] message  给人看的说明
     * @note 在 UI 线程里发出
     */
    void errorOccurred(const QString &message);

private:
    /**
     * @brief 视频线程交来一帧：包成 QImage（不复制像素），排进 UI 线程再发信号
     * @param[in] picture  RGBX 画面
     * @return 无
     * @note 在视频线程里被调用。界面来不及显示（已经有 2 帧排着）时直接丢掉，防止越积越多
     */
    void handlePictureFromWorker(RgbxPicture picture);

    /**
     * @brief 视频或音频线程报告"这个 serial 播完了"，两边都播完时发 playbackFinished
     * @param[in] isVideo  true = 视频线程报告的；false = 音频线程
     * @param[in] serial   播完的 serial
     * @return 无
     * @note 在工作线程里被调用
     */
    void handleWorkerFinished(bool isVideo, int serial);

    /**
     * @brief 工作线程报告错误，排进 UI 线程发 errorOccurred
     * @param[in] message  说明
     * @return 无
     * @note 在工作线程里被调用
     */
    void handleWorkerError(const std::string &message);

    Mp4Demuxer demuxer;                          // 拆封装（解封装线程用）
    MediaClock clock;                            // 播放时钟
    PlaybackState state;                         // 共用状态
    DesktopVolumeWatcher desktopVolumeWatcher;   // 盯着正点原子桌面的音量文件（板子上的音量键改的是它）
    std::unique_ptr<PacketQueue> videoQueuePtr;  // 视频包队列；没有视频时是空的
    std::unique_ptr<PacketQueue> audioQueuePtr;  // 音频包队列；没有音频或不播声音时是空的
    std::unique_ptr<DemuxWorker> demuxWorkerPtr; // 解封装线程要做的事
    std::unique_ptr<VideoWorker> videoWorkerPtr; // 视频线程要做的事
    std::unique_ptr<AudioWorker> audioWorkerPtr; // 音频线程要做的事
    std::thread demuxThread;                     // 解封装线程
    std::thread videoThread;                     // 视频线程
    std::thread audioThread;                     // 音频线程

    bool hasVideo = false;                       // 有视频流
    bool hasAudio = false;                       // 有音频流并且要播声音
    std::atomic<int> videoFinishedSerial{-1};    // 视频播完的 serial
    std::atomic<int> audioFinishedSerial{-1};    // 音频播完的 serial
    std::atomic<int> pendingPictureCount{0};     // 已经交给 UI 线程、还没显示的画面数
    std::atomic<int> droppedPendingPictureCount{0}; // 因为界面来不及而丢掉的画面数

    static constexpr int kMaxPendingPictureCount = 2;  // 最多排着几帧没显示
    static constexpr size_t kVideoQueueCapacity = 128; // 视频包队列容量（30 fps 约 4 秒）
    static constexpr size_t kAudioQueueCapacity = 256; // 音频包队列容量（48 kHz AAC 约 5 秒）
};
