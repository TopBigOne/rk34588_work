#include "ui/touch_seek_slider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

/**
 * @brief 建一个横向进度条
 * @param[in] parent  Qt 父对象
 */
TouchSeekSlider::TouchSeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

/**
 * @brief 按下：先把滑块挪到按下的位置，再交给 QSlider
 * @param[in] event  按下事件
 * @return 无
 * @note 第 1 步 问 style 轨道（groove）和滑块（handle）画在哪里；第 2 步 把按下的 x 坐标换算成进度值；
 *       第 3 步 挪滑块；第 4 步 交给 QSlider 原来的处理
 */
void TouchSeekSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { // 触摸转换成的鼠标事件是左键
        QSlider::mousePressEvent(event);
        return;
    }

    // 第 1 步 QStyleOptionSlider：画进度条要用的参数（范围、当前值、方向……）；initStyleOption 按本控件的状态填好。
    // subControlRect(控件类型, 参数, 子部件, 控件)：这个子部件画在哪个矩形里（样式表里的 groove、handle 大小都会算进去）
    QStyleOptionSlider sliderOption;
    initStyleOption(&sliderOption);
    const QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &sliderOption, QStyle::SC_SliderGroove, this);
    const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &sliderOption, QStyle::SC_SliderHandle, this);

    // 第 2 步 滑块的左边缘能走的范围是 [轨道左端, 轨道右端 − 滑块宽度]，让滑块的中心对准手指：左边缘 = 手指 x − 滑块宽度 / 2。
    // QStyle::sliderValueFromPosition(最小值, 最大值, 位置, 总长度, 是否反向)：把"走了多远"按比例换算成进度值
    const int handleTravelPx = grooveRect.width() - handleRect.width();
    const int handleLeftPx = event->pos().x() - handleRect.width() / 2 - grooveRect.x();
    if (handleTravelPx > 0) {
        const int pressedValue = QStyle::sliderValueFromPosition(minimum(), maximum(), handleLeftPx, handleTravelPx,
                                                                 sliderOption.upsideDown);
        // 第 3 步 setSliderPosition：挪滑块（拖动时用的那个位置）；setValue：同时改值。两个都设，下面 QSlider 看到的就是新位置
        setSliderPosition(pressedValue);
        setValue(pressedValue);
    }

    // 第 4 步 这时滑块已经在手指下面，QSlider 认为"按中了滑块"，进入拖动状态，并发 sliderPressed 信号
    QSlider::mousePressEvent(event);
}
