#include "ui/player_window.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>

/**
 * @brief 构造
 * @param[in] parent  父控件
 * @note 第 1 步 建控件；第 2 步 排布局；第 3 步 连信号和槽；第 4 步 启动刷新进度的定时器
 */
PlayerWindow::PlayerWindow(QWidget *parent) : QWidget(parent) {
    // 第 1 步 建控件。new 的时候传 this 当父对象：窗口析构时 Qt 自动 delete 它们，不用手动释放
    playerController = new PlayerController(this);
    videoWidget = new VideoWidget(this);
    playPauseButton = new QPushButton(QStringLiteral("暂停"), this);
    positionSlider = new TouchSeekSlider(this); // 横向进度条，按哪里跳哪里（见 touch_seek_slider.h）
    timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    exitButton = new QPushButton(QStringLiteral("退出"), this);
    statusLabel = new QLabel(this);
    progressTimer = new QTimer(this);

    // 触摸屏上手指点按钮：按钮做高一点、字大一点。
    // 文字颜色、背景色要写明：窗口的样式表 background-color: black 会传给所有子控件，按钮背景也变成黑的，
    // 而按钮文字默认是黑色 → 黑底黑字，只看得见边框（2026-11-19 上板截图发现）。
    // 字设成白色，背景用深灰 #333333 和窗口的黑色区分开；按下时（:pressed）变成更浅的灰，手指按下去有反馈
    const QString buttonStyle = QStringLiteral(
            "QPushButton { font-size: 36px; min-height: 96px; min-width: 160px; color: white;"
            " background-color: #333333; border: 2px solid #888888; border-radius: 12px; }"
            "QPushButton:pressed { background-color: #555555; }");
    playPauseButton->setStyleSheet(buttonStyle);
    exitButton->setStyleSheet(buttonStyle);
    timeLabel->setStyleSheet(QStringLiteral("font-size: 32px; color: white;"));
    statusLabel->setStyleSheet(QStringLiteral("font-size: 28px; color: #E8A908;"));
    // 进度条的滑块也做大：groove 是轨道，handle 是滑块
    positionSlider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal { height: 16px; background: #555555; border-radius: 8px; }"
            "QSlider::sub-page:horizontal { background: #0E83E3; border-radius: 8px; }"
            "QSlider::handle:horizontal { width: 56px; margin: -20px 0; background: white; border-radius: 28px; }"));
    positionSlider->setMinimumHeight(96);
    setStyleSheet(QStringLiteral("background-color: black;"));

    // 第 2 步 布局：竖着排（画面在上，占掉所有多余空间；控制条、状态栏在下）
    auto *controlLayout = new QHBoxLayout; // 一排控制按钮，横着排
    controlLayout->addWidget(playPauseButton);
    controlLayout->addWidget(positionSlider, 1); // 第二个参数 1 = 伸展系数：多余的宽度都给进度条
    controlLayout->addWidget(timeLabel);
    controlLayout->addWidget(exitButton);

    auto *mainLayout = new QVBoxLayout(this); // 传 this：这个布局管理整个窗口
    mainLayout->setContentsMargins(0, 0, 0, 16);
    mainLayout->addWidget(videoWidget, 1); // 伸展系数 1：多余的高度都给画面
    mainLayout->addLayout(controlLayout);
    mainLayout->addWidget(statusLabel);

    // 第 3 步 连信号和槽：connect(发送者, 信号, 接收者, 槽)。
    // 画面：controller 发 videoPictureReady → videoWidget 显示
    connect(playerController, &PlayerController::videoPictureReady, videoWidget, &VideoWidget::showPicture);
    connect(playerController, &PlayerController::playbackFinished, this, &PlayerWindow::handlePlaybackFinished);
    connect(playerController, &PlayerController::errorOccurred, this, &PlayerWindow::handlePlayerError);
    // 画面区域大小变了 → 告诉 controller，RGA 按新大小缩放（接收者是 lambda，直接写）
    connect(videoWidget, &VideoWidget::sizeChanged, this,
            [this](int width, int height) { playerController->setVideoBoxSize(width, height); });
    connect(playPauseButton, &QPushButton::clicked, this, &PlayerWindow::handlePlayPauseClicked);
    connect(positionSlider, &QSlider::sliderReleased, this, &PlayerWindow::handleSliderReleased);
    connect(positionSlider, &QSlider::sliderMoved, this, &PlayerWindow::handleSliderMoved);
    // sliderPressed：按下的那一刻（TouchSeekSlider 已经把滑块挪到手指下了）就 seek 一次，点一下也能跳过去
    connect(positionSlider, &QSlider::sliderPressed, this, [this]() {
        dragSeekTimer.start();
        playerController->seekTo(static_cast<int64_t>(positionSlider->sliderPosition()) * 1000);
    });
    connect(positionSlider, &QSlider::actionTriggered, this, &PlayerWindow::handleSliderAction);
    // 退出：QApplication::quit 让 main 里的 app.exec() 返回
    connect(exitButton, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(progressTimer, &QTimer::timeout, this, &PlayerWindow::refreshProgress);

    // 第 4 步 每 200 ms 刷新一次进度
    progressTimer->start(200);
}

/**
 * @brief 析构
 * @return 无
 * @note 先停线程，再让 Qt 删子控件
 */
PlayerWindow::~PlayerWindow() { playerController->stop(); }

/**
 * @brief 停止播放
 * @return 无
 * @note 交给 PlayerController::stop()，它可以重复调用（析构时还会再调一次，不会出问题）
 */
void PlayerWindow::stopPlayback() { playerController->stop(); }

/**
 * @brief 打开文件
 * @param[in] filePath         路径
 * @param[in] shouldPlayAudio  要不要声音
 * @return true = 成功；false = 失败
 * @note 进度条单位是毫秒（QSlider 只能用 int，微秒会超出 int 的范围：int 最大约 2147 秒）
 */
bool PlayerWindow::openFile(const QString &filePath, bool shouldPlayAudio) {
    QString errorMessage;
    if (!playerController->openFile(filePath, shouldPlayAudio, errorMessage)) {
        statusLabel->setText(errorMessage);
        playPauseButton->setEnabled(false);
        return false;
    }
    const int durationMs = static_cast<int>(playerController->getDurationUs() / 1000);
    positionSlider->setRange(0, durationMs);
    positionSlider->setPageStep(10 * 1000); // 点进度条空白处一次跳 10 秒
    statusLabel->setText(filePath + (shouldPlayAudio ? QString() : QStringLiteral("（不播声音）")));
    return true;
}

/**
 * @brief 播放/暂停 按钮
 * @return 无
 * @note 播完了：seek 到开头并继续播放
 */
void PlayerWindow::handlePlayPauseClicked() {
    if (isPlaybackFinished) {
        isPlaybackFinished = false;
        playerController->seekTo(0);
        playerController->setPaused(false);
        playPauseButton->setText(QStringLiteral("暂停"));
        return;
    }
    const bool shouldPause = !playerController->isPaused();
    playerController->setPaused(shouldPause);
    playPauseButton->setText(shouldPause ? QStringLiteral("播放") : QStringLiteral("暂停"));
}

/**
 * @brief 松开进度条
 * @return 无
 * @note 拖动以后可能已经不在结尾了，清掉"播完了"的状态
 */
void PlayerWindow::handleSliderReleased() {
    isPlaybackFinished = false;
    playPauseButton->setText(playerController->isPaused() ? QStringLiteral("播放") : QStringLiteral("暂停"));
    playerController->seekTo(static_cast<int64_t>(positionSlider->sliderPosition()) * 1000);
}

/**
 * @brief 拖动中：时间文字跟着手指，隔一段时间 seek 一次
 * @param[in] positionMs  滑块位置，毫秒
 * @return 无
 * @note QElapsedTimer::elapsed()：从上次 start() 到现在过了多少毫秒；restart() 返回同样的值并重新计时
 */
void PlayerWindow::handleSliderMoved(int positionMs) {
    const int64_t positionUs = static_cast<int64_t>(positionMs) * 1000;
    timeLabel->setText(formatTime(positionUs) + QStringLiteral(" / ") + formatTime(playerController->getDurationUs()));
    if (dragSeekTimer.elapsed() >= kDragSeekIntervalMs) {
        dragSeekTimer.restart();
        playerController->seekTo(positionUs);
    }
}

/**
 * @brief 进度条翻页
 * @param[in] action  动作类型
 * @return 无
 * @note 拖动（SliderMove）在松手时统一处理，这里只管翻页
 */
void PlayerWindow::handleSliderAction(int action) {
    if (action == QAbstractSlider::SliderPageStepAdd || action == QAbstractSlider::SliderPageStepSub) {
        handleSliderReleased();
    }
}

/**
 * @brief 刷新进度
 * @return 无
 * @note isSliderDown()：手指正按着滑块
 */
void PlayerWindow::refreshProgress() {
    if (positionSlider->isSliderDown()) {
        return; // 手指按着：进度条和时间文字都由 handleSliderMoved 跟着手指更新，这里不抢
    }
    const int64_t positionUs = playerController->getPositionUs();
    positionSlider->setValue(static_cast<int>(positionUs / 1000));
    timeLabel->setText(formatTime(positionUs) + QStringLiteral(" / ") + formatTime(playerController->getDurationUs()));
}

/**
 * @brief 播完了
 * @return 无
 * @note 可能收到两次，重复设置没关系
 */
void PlayerWindow::handlePlaybackFinished() {
    isPlaybackFinished = true;
    playPauseButton->setText(QStringLiteral("重播"));
    statusLabel->setText(QStringLiteral("播放结束"));
}

/**
 * @brief 显示错误
 * @param[in] message  说明
 * @return 无
 * @note 只显示最近一条
 */
void PlayerWindow::handlePlayerError(const QString &message) { statusLabel->setText(message); }

/**
 * @brief 微秒 → "分:秒"
 * @param[in] timeUs  微秒
 * @return 文字
 * @note arg(数, 宽度, 进制, 填充字符)：宽度 2、不够补 '0'，5 → "05"
 */
QString PlayerWindow::formatTime(int64_t timeUs) {
    const int64_t totalSeconds = timeUs / 1000000;
    return QStringLiteral("%1:%2")
            .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
            .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}
