#pragma once

#include <QImage>
#include <QWidget>

/**
 * @brief 显示画面的控件：保存最新的一帧，重画时把它居中贴上去，四周填黑
 * @note 画面在视频线程里已经由 RGA 缩放成正好放得下的大小，这里只贴图，不缩放（CPU 缩放 1080p 每帧要好几毫秒）
 */
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 构造
     * @param[in] parent  父控件
     * @note 背景设成不透明：我们自己把整块区域画满，Qt 不用先擦背景
     */
    explicit VideoWidget(QWidget *parent = nullptr);

public slots:
    /**
     * @brief 换成新的一帧并安排重画
     * @param[in] picture  新画面
     * @return 无
     * @note update() 不是马上重画，而是告诉 Qt"这块要重画"，Qt 在事件循环里合并多次请求后调用一次 paintEvent
     */
    void showPicture(const QImage &picture);

signals:
    /**
     * @brief 控件大小变了
     * @param[in] width   新的宽，像素
     * @param[in] height  新的高，像素
     * @note 播放器据此让 RGA 缩放到新的大小
     */
    void sizeChanged(int width, int height);

protected:
    /**
     * @brief 重画：填黑，再把当前画面居中贴上
     * @param[in] event  重画事件（哪块区域要重画），这里整块都画，用不到
     * @return 无
     * @note 由 Qt 在 UI 线程里调用，不要自己调用
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 大小变了：发 sizeChanged 信号
     * @param[in] event  里面有新旧大小
     * @return 无
     * @note 窗口第一次显示、全屏、旋转时都会触发
     */
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage currentPicture; // 当前显示的一帧（QImage 是隐式共享的，赋值只是引用计数加 1，不复制像素）
};
