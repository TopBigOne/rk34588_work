#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include "player/player_controller.h"
#include "ui/touch_seek_slider.h"
#include "ui/video_widget.h"

/**
 * @brief 播放器窗口：上面是画面，下面一排是 播放/暂停 按钮、进度条、时间、退出按钮，最下面一行是状态提示
 * @note 板子屏幕是竖屏触摸屏（1080×1920），按钮和字都做得比较大，手指好点
 */
class PlayerWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造：建控件、排布局、连信号
     * @param[in] parent  父控件；顶层窗口传 nullptr
     * @note 还不打开文件
     */
    explicit PlayerWindow(QWidget *parent = nullptr);

    /**
     * @brief 析构：先停播放器的线程
     * @return 无
     * @note 子控件（包括 playerController）由 Qt 在 QObject 析构时自动 delete；
     *       先 stop 是为了不让工作线程在控件析构的过程中还往这边发画面
     */
    ~PlayerWindow() override;

    /**
     * @brief 停止播放：停掉解封装、解码、声音线程，关掉声卡
     * @return 无
     * @note 点了「退出」、事件循环结束后，main 先调它让声音立刻停，再做别的收尾（请桌面显示回来）。可以重复调用
     */
    void stopPlayback();

    /**
     * @brief 打开文件开始播放
     * @param[in] filePath         mp4 路径
     * @param[in] shouldPlayAudio  false = 只播画面
     * @return true = 成功；false = 失败（状态栏显示原因）
     * @note 只能调用一次
     */
    bool openFile(const QString &filePath, bool shouldPlayAudio);

private slots:
    /**
     * @brief 点了 播放/暂停 按钮：播完了就从头重播，否则在暂停和继续之间切换
     * @return 无
     * @note 按钮文字跟着变
     */
    void handlePlayPauseClicked();

    /**
     * @brief 松开进度条：跳到松手的位置
     * @return 无
     * @note 拖动过程中 handleSliderMoved 已经隔一段时间 seek 过，松手时再按最终位置 seek 一次
     */
    void handleSliderReleased();

    /**
     * @brief 手指拖着进度条在动：时间文字跟着手指走，每隔 kDragSeekIntervalMs 毫秒 seek 一次，画面跟着变
     * @param[in] positionMs  滑块现在的位置，毫秒
     * @return 无
     * @note 不是每动一下都 seek：每次 seek 都要清空队列、重置解码器、从关键帧重新解，太频繁画面反而一直出不来
     */
    void handleSliderMoved(int positionMs);

    /**
     * @brief 点了进度条的空白处（翻页）：跳到新位置
     * @param[in] action  QSlider 的动作类型；只处理翻页（SliderPageStepAdd / Sub）
     * @return 无
     * @note 这个信号发出时 sliderPosition() 已经是新位置了
     */
    void handleSliderAction(int action);

    /**
     * @brief 定时刷新进度条和时间（每 200 ms）
     * @return 无
     * @note 用户正在拖进度条时不刷新进度条，否则会和手指抢
     */
    void refreshProgress();

    /**
     * @brief 画面和声音都播完了
     * @return 无
     * @note 按钮变成"重播"
     */
    void handlePlaybackFinished();

    /**
     * @brief 播放器报错：显示在状态栏
     * @param[in] message  说明
     * @return 无
     * @note 不弹对话框，不打断播放
     */
    void handlePlayerError(const QString &message);

private:
    /**
     * @brief 把微秒换成 "分:秒" 文字
     * @param[in] timeUs  微秒
     * @return 例如 "01:05"
     * @note 不到 1 秒的部分舍掉
     */
    static QString formatTime(int64_t timeUs);

    PlayerController *playerController; // 播放器总控（子对象，Qt 负责 delete）
    VideoWidget *videoWidget;           // 画面
    QPushButton *playPauseButton;       // 播放 / 暂停 / 重播
    TouchSeekSlider *positionSlider;    // 进度条，单位毫秒（按哪里跳哪里，适合手指）
    QLabel *timeLabel;                  // "当前 / 总长"
    QPushButton *exitButton;            // 退出
    QLabel *statusLabel;                // 状态提示（错误、播完了）
    QTimer *progressTimer;              // 刷新进度的定时器
    bool isPlaybackFinished = false;    // 已经播完了（按钮是"重播"）
    QElapsedTimer dragSeekTimer;        // 拖动时距离上一次 seek 过了多久

    static constexpr int kDragSeekIntervalMs = 250; // 拖动时最快每 250 ms seek 一次
};
