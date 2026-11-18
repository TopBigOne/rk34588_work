#include "ui/video_widget.h"

#include <QPainter>
#include <QResizeEvent>

/**
 * @brief 构造
 * @param[in] parent  父控件
 * @note WA_OpaquePaintEvent：告诉 Qt "paintEvent 会把整块区域画满"，Qt 就不先填背景色，少画一遍
 */
VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent) { setAttribute(Qt::WA_OpaquePaintEvent); }

/**
 * @brief 换成新的一帧
 * @param[in] picture  新画面
 * @return 无
 * @note 旧的那一帧在这里被替换掉，引用计数到 0 时调用清理函数，像素内存释放
 */
void VideoWidget::showPicture(const QImage &picture) {
    currentPicture = picture;
    update();
}

/**
 * @brief 重画
 * @param[in] event  重画事件
 * @return 无
 * @note 第 1 步 整块填黑；第 2 步 画面居中贴上
 */
void VideoWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event); // 告诉编译器"这个参数故意不用"，免得 -Wextra 报未使用参数
    // QPainter：在控件上画东西的"画笔"，构造时开始画，析构时结束
    QPainter painter(this);

    // 第 1 步 填黑：画面四周（上下或左右）留出来的地方是黑边
    painter.fillRect(rect(), Qt::black);

    // 第 2 步 居中：左上角 = (控件宽 − 画面宽) / 2，(控件高 − 画面高) / 2
    if (!currentPicture.isNull()) {
        const int pictureX = (width() - currentPicture.width()) / 2;
        const int pictureY = (height() - currentPicture.height()) / 2;
        painter.drawImage(pictureX, pictureY, currentPicture); // 原样贴，不缩放
    }
}

/**
 * @brief 大小变了
 * @param[in] event  大小变化事件
 * @return 无
 * @note 先调用父类的实现，再发信号
 */
void VideoWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    emit sizeChanged(event->size().width(), event->size().height());
}
