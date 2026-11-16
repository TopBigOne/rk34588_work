#include "ui/click_counter_window.h"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kWindowWidthPixels = 1080;
constexpr int kWindowHeightPixels = 1920;
constexpr int kFontPointSize = 24; // 字号，单位是 point（磅），板子屏幕上太小的字不好看清
} // namespace

ClickCounterWindow::ClickCounterWindow(QWidget *parentWidget) : QWidget(parentWidget) {
    // 第 1 步 建控件：this 作为父对象 → 本窗口销毁时自动 delete 子控件
    clickCountLabel = new QLabel("Hello RK3588 Qt\n已按 0 次", this);
    clickCountLabel->setAlignment(Qt::AlignCenter); // 文字在控件里水平、垂直都居中
    clickButton = new QPushButton("按我", this);

    QFont bigFont = font(); // 先拿窗口当前的字体，只改字号
    bigFont.setPointSize(kFontPointSize);
    clickCountLabel->setFont(bigFont);
    clickButton->setFont(bigFont);

    // 第 2 步 排版：QVBoxLayout 把控件从上到下排；构造时传 this → 这个布局管理本窗口
    auto *verticalLayout = new QVBoxLayout(this);
    verticalLayout->addWidget(clickCountLabel);
    verticalLayout->addWidget(clickButton);

    // 第 3 步 连信号槽：按钮发出 clicked 信号时，调用本窗口的 increaseClickCount。
    // 写成 &类::函数 的形式，信号名、槽名写错时编译就会报错
    connect(clickButton, &QPushButton::clicked, this, &ClickCounterWindow::increaseClickCount);

    setWindowTitle("day7_qt_env");
    resize(kWindowWidthPixels, kWindowHeightPixels);
}

void ClickCounterWindow::increaseClickCount() {
    clickCount++;
    // QString::arg：把 %1 替换成参数的值
    clickCountLabel->setText(QString("Hello RK3588 Qt\n已按 %1 次").arg(clickCount));
}
