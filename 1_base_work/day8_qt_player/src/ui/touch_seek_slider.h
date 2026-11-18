#pragma once

#include <QSlider>

class QMouseEvent;

/**
 * @brief 适合手指操作的进度条：按在哪里，滑块就跳到哪里，并且接着就能拖
 * @note 普通 QSlider 是按鼠标设计的：只有按中滑块才能拖；按在滑块以外的轨道上是"翻页"（一次跳一个 pageStep）。
 *       手指比鼠标指针粗，很难正好按中滑块，结果就是一跳一跳、滑块不跟手（2026-11-19 用户上板反馈"好卡、不能随手指滑动"）。
 *       这里在按下时先把滑块挪到手指下面，再交给 QSlider 原来的处理：这时按的位置就在滑块上，QSlider 就进入拖动状态
 */
class TouchSeekSlider : public QSlider {
public:
    /**
     * @brief 建一个横向进度条
     * @param[in] parent  Qt 父对象
     */
    explicit TouchSeekSlider(QWidget *parent = nullptr);

protected:
    /**
     * @brief 手指（鼠标）按下：先把滑块挪到按下的位置，再按 QSlider 原来的方式处理（开始拖动）
     * @param[in] event  按下事件，event->pos() 是相对本控件左上角的坐标
     * @return 无
     * @note 触摸屏的点按，Qt 会转换成鼠标事件送过来，所以重写 mousePressEvent 就够了
     */
    void mousePressEvent(QMouseEvent *event) override;
};
