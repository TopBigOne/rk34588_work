#include "ui/nvr_preview_window.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/video_tile_widget.h"

namespace {
constexpr int kButtonFontPointSize = 22;
constexpr int kButtonHeightPixels = 120;  // 按钮高一点，手指好按
constexpr int kTileSpacingPixels = 4;     // 格子之间的缝，单位像素
constexpr int kWindowMarginPixels = 8;    // 窗口内容离窗口边缘的距离，单位像素
const char *const kWindowStyleSheet = "NvrPreviewWindow { background-color: #212121; }";

/**
 * @brief 建一个按钮，统一字号和高度
 * @param[in] buttonText    按钮上的文字
 * @param[in] parentWidget  父控件；销毁时 Qt 自动 delete 这个按钮
 * @return 新建的按钮，归 parentWidget 管，调用方不用 delete
 */
QPushButton *createButton(const QString &buttonText, QWidget *parentWidget) {
    auto *button = new QPushButton(buttonText, parentWidget);
    QFont buttonFont = button->font();
    buttonFont.setPointSize(kButtonFontPointSize);
    button->setFont(buttonFont);
    button->setMinimumHeight(kButtonHeightPixels);
    return button;
}
} // namespace

NvrPreviewWindow::NvrPreviewWindow(QWidget *parentWidget) : QWidget(parentWidget) {
    // 样式表只对 NvrPreviewWindow 这个类名生效；自定义的 QWidget 子类要打开 WA_StyledBackground，
    // 背景色才会真的画出来（QWidget 默认不画样式表里的背景）
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(kWindowStyleSheet);

    // 第 1 步 四格画面放进 2×2 网格：第 i 路放在第 i / 2 行、第 i % 2 列
    tileGridLayout = new QGridLayout; // 先不指定父对象，第 3 步 addLayout 时归外层布局管
    tileGridLayout->setSpacing(kTileSpacingPixels);
    for (int channelIndex = 0; channelIndex < kChannelCount; channelIndex++) {
        videoTiles[channelIndex] = new VideoTileWidget(channelIndex, this);
        tileGridLayout->addWidget(videoTiles[channelIndex],
                                  channelIndex / kGridColumnCount,  // 行
                                  channelIndex % kGridColumnCount); // 列
        // 点了某一格 → selectChannel(那一格的通道号)。信号的参数会原样传给槽
        connect(videoTiles[channelIndex], &VideoTileWidget::tileClicked, this, &NvrPreviewWindow::selectChannel);
    }

    // 第 2 步 一排按钮放进水平布局：从左到右，平分宽度
    QPushButton *singleChannelButton = createButton("单画面", this);
    QPushButton *allChannelsButton = createButton("四画面", this);
    QPushButton *previousChannelButton = createButton("上一路", this);
    QPushButton *nextChannelButton = createButton("下一路", this);
    QPushButton *quitButton = createButton("退出", this);

    auto *buttonRowLayout = new QHBoxLayout;
    buttonRowLayout->addWidget(singleChannelButton);
    buttonRowLayout->addWidget(allChannelsButton);
    buttonRowLayout->addWidget(previousChannelButton);
    buttonRowLayout->addWidget(nextChannelButton);
    buttonRowLayout->addWidget(quitButton);

    connect(singleChannelButton, &QPushButton::clicked, this, &NvrPreviewWindow::showSingleChannel);
    connect(allChannelsButton, &QPushButton::clicked, this, &NvrPreviewWindow::showAllChannels);
    connect(previousChannelButton, &QPushButton::clicked, this, &NvrPreviewWindow::selectPreviousChannel);
    connect(nextChannelButton, &QPushButton::clicked, this, &NvrPreviewWindow::selectNextChannel);
    // QApplication::quit：让 app.exec() 的事件循环结束并返回 0，程序就退出了
    connect(quitButton, &QPushButton::clicked, qApp, &QApplication::quit);

    // 第 3 步 外层从上到下：画面网格 → 按钮行 → 弹簧
    // 竖屏 1080×1920（2026-11-17 实测 /sys/class/drm/card0-DSI-1/modes），四格 16:9 只占上面 600 多像素，
    // 弹簧把剩下的空白挤到最下面，画面和按钮就贴在屏幕上方
    auto *windowLayout = new QVBoxLayout(this); // 传 this → 这个布局管理本窗口
    windowLayout->setContentsMargins(kWindowMarginPixels, kWindowMarginPixels, kWindowMarginPixels,
                                     kWindowMarginPixels);
    windowLayout->addLayout(tileGridLayout);
    windowLayout->addLayout(buttonRowLayout);
    windowLayout->addStretch(); // 弹簧：没有内容，只占掉剩下的空间

    setWindowTitle("day7 NVR 预览");
    selectChannel(0);
}

void NvrPreviewWindow::selectChannel(int requestedChannelIndex) {
    selectedChannelIndex = requestedChannelIndex;
    for (int channelIndex = 0; channelIndex < kChannelCount; channelIndex++) {
        videoTiles[channelIndex]->setSelectedStyle(channelIndex == selectedChannelIndex);
    }
    updateTileVisibility(); // 单画面模式下，显示的那一格要换成新选中的那一路
}

void NvrPreviewWindow::showSingleChannel() {
    isSingleChannelMode = true;
    updateTileVisibility();
}

void NvrPreviewWindow::showAllChannels() {
    isSingleChannelMode = false;
    updateTileVisibility();
}

void NvrPreviewWindow::selectPreviousChannel() {
    // + kChannelCount 再取余：0 - 1 = -1 时变成 3，C++ 里 -1 % 4 是 -1，不是 3
    selectChannel((selectedChannelIndex - 1 + kChannelCount) % kChannelCount);
}

void NvrPreviewWindow::selectNextChannel() {
    selectChannel((selectedChannelIndex + 1) % kChannelCount);
}

void NvrPreviewWindow::updateTileVisibility() {
    for (int channelIndex = 0; channelIndex < kChannelCount; channelIndex++) {
        bool shouldShowTile = !isSingleChannelMode || channelIndex == selectedChannelIndex;
        videoTiles[channelIndex]->setVisible(shouldShowTile);
    }
    // 单画面时只剩一格，它占着网格里的某一行某一列。另一列空了，网格只剩一列，
    // 这一格就占满整个宽度（隐藏的控件不占位置）
}
