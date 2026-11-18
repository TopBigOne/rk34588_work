#include "ui/video_tile_widget.h"

#include <QSizePolicy>

namespace {
constexpr int kAspectWidth = 16; // 画面宽高比 16:9 的"16"
constexpr int kAspectHeight = 9; // 画面宽高比 16:9 的"9"
constexpr int kTileFontPointSize = 28;

// Qt 样式表（写法和网页的 CSS 差不多）：黑底、灰字、边框。
// 没选中时边框是深灰色，选中时换成橙色；两个样式边框粗细一样，切换时画面不会跳动
const char *const kNormalStyleSheet =
    "VideoTileWidget { background-color: black; color: #9e9e9e; border: 4px solid #424242; }";
const char *const kSelectedStyleSheet =
    "VideoTileWidget { background-color: black; color: #9e9e9e; border: 4px solid #ff9800; }";
} // namespace

VideoTileWidget::VideoTileWidget(int requestedChannelIndex, QWidget *parentWidget)
    : QLabel(parentWidget), channelIndex(requestedChannelIndex) {
    // 第 1 步 占位文字：通道号显示给用户时从 1 开始
    setText(QString("通道 %1\n无信号").arg(channelIndex + 1));
    setAlignment(Qt::AlignCenter);

    QFont tileFont = font();
    tileFont.setPointSize(kTileFontPointSize);
    setFont(tileFont);

    // 第 2 步 大小策略：宽度跟着布局伸缩（Expanding），高度由 heightForWidth 算出来。
    // setHeightForWidth(true)：告诉布局"我的高度依赖宽度"，布局才会去调 heightForWidth
    QSizePolicy tileSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tileSizePolicy.setHeightForWidth(true);
    setSizePolicy(tileSizePolicy);

    setSelectedStyle(false);
}

void VideoTileWidget::setSelectedStyle(bool isSelected) {
    setStyleSheet(isSelected ? kSelectedStyleSheet : kNormalStyleSheet);
}

bool VideoTileWidget::hasHeightForWidth() const {
    return true;
}

int VideoTileWidget::heightForWidth(int widthPixels) const {
    return widthPixels * kAspectHeight / kAspectWidth;
}

void VideoTileWidget::mousePressEvent(QMouseEvent * /*mouseEvent*/) {
    // emit 是空宏，只是让读代码的人一眼看出"这里在发信号"；
    // 发出去之后，Qt 会调用所有 connect 到这个信号的槽函数
    emit tileClicked(channelIndex);
}
