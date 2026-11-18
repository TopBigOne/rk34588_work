//
// VideoTileWidget：NVR 画面里的一格（一路摄像头的显示区域）
// 现在只显示"通道 N / 无信号"的占位文字；以后把 MPP 解码出来的帧画到这里。
// 宽高比固定 16:9：告诉布局"给我多宽，我就要宽 × 9 / 16 那么高"（heightForWidth）
//
#pragma once

#include <QLabel>

class VideoTileWidget : public QLabel {
    // Q_OBJECT：本类有自己的信号 tileClicked，moc 要为它生成代码
    Q_OBJECT

public:
    /**
     * @brief 建一格画面，显示"通道 N / 无信号"
     * @param[in] requestedChannelIndex  这一格对应第几路，从 0 开始；显示给用户时 +1
     * @param[in] parentWidget           父控件；销毁时 Qt 自动 delete 本控件
     * @return 无（构造函数）
     * @note 参数叫 requestedChannelIndex，不和成员 channelIndex 同名（规范：防止参数赋给自己）
     */
    explicit VideoTileWidget(int requestedChannelIndex, QWidget *parentWidget = nullptr);

    /**
     * @brief 设置这一格是不是被选中的那一路：选中时边框变成高亮色
     * @param[in] isSelected  true = 选中；false = 没选中
     * @return 无
     */
    void setSelectedStyle(bool isSelected);

    /**
     * @brief 告诉布局：本控件的高度由宽度决定
     * @return 总是 true
     * @note QWidget 的虚函数，布局分配大小时会调用
     */
    bool hasHeightForWidth() const override;

    /**
     * @brief 给定宽度，返回本控件想要的高度（保持 16:9）
     * @param[in] widthPixels  布局打算分给本控件的宽度，单位像素
     * @return 想要的高度，单位像素 = widthPixels × 9 / 16
     * @note QWidget 的虚函数，布局分配大小时会调用
     */
    int heightForWidth(int widthPixels) const override;

signals:
    /**
     * @brief 这一格被点了（鼠标点击或触摸）
     * @param[in] clickedChannelIndex  被点的是第几路，从 0 开始
     * @note signals 只声明、不用写函数体，函数体由 moc 生成
     */
    void tileClicked(int clickedChannelIndex);

protected:
    /**
     * @brief 鼠标（或触摸）按下时调用：发出 tileClicked 信号
     * @param[in] mouseEvent  按下的位置、哪个键；这里不关心，只要按下就算点了
     * @return 无
     * @note QWidget 的虚函数。触摸屏上手指按下，Qt 默认也会转成鼠标按下事件送到这里
     */
    void mousePressEvent(QMouseEvent *mouseEvent) override;

private:
    int channelIndex = 0; // 这一格是第几路，从 0 开始
};
