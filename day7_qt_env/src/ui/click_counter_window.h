//
// ClickCounterWindow：验证 Qt 开发环境用的最小窗口
// 一行文字 + 一个按钮，每按一次按钮，文字里的次数加 1。
// 能看到窗口 → 显示没问题；按钮有反应 → 触摸/鼠标输入没问题；次数会变 → moc 生成的信号槽没问题
//
#pragma once

#include <QWidget>

class QLabel;      // 前置声明：头文件里只用到指针，不需要完整定义，省得包含 <QLabel>
class QPushButton;

class ClickCounterWindow : public QWidget {
    // Q_OBJECT：告诉 moc 这个类要用信号槽。moc 会生成 moc_click_counter_window.cpp，
    // 里面是信号槽要用的元信息（类名、槽函数表）。不写的话 connect 到本类的槽会失败
    Q_OBJECT

public:
    /**
     * @brief 创建窗口里的文字和按钮，并把按钮的 clicked 信号连到 increaseClickCount
     * @param[in] parentWidget  父窗口；nullptr = 自己就是顶层窗口
     * @return 无（构造函数）
     * @note 子控件都以本窗口为父对象，窗口销毁时 Qt 会自动 delete 它们，不用手动释放
     */
    explicit ClickCounterWindow(QWidget *parentWidget = nullptr);

private slots:
    /**
     * @brief 按钮被按下一次：次数加 1，并刷新文字
     * @return 无
     * @note 由按钮的 clicked 信号触发，在 GUI 线程里执行
     */
    void increaseClickCount();

private:
    QLabel *clickCountLabel = nullptr;  // 显示"已按 N 次"的文字控件，父对象是本窗口
    QPushButton *clickButton = nullptr; // 让用户按的按钮，父对象是本窗口
    int clickCount = 0;                 // 按钮一共被按了几次
};
