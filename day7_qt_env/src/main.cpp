//
// day7_qt_env：验证 Mac + CLion 给板子开发 Qt 程序的环境
// 用法：./day7_qt_env [nvr | click]（不写参数默认 nvr）
//   nvr    NVR 预览界面：2×2 四路画面 + 一排按钮 → ui/nvr_preview_window.cpp
//   click  按按钮计数的测试窗口（验证环境用）    → ui/click_counter_window.cpp
//
#include <QApplication>
#include <cstring> // strcmp

#include "ui/click_counter_window.h"
#include "ui/nvr_preview_window.h"

/**
 * @brief 程序入口：创建 Qt 应用，按第一个参数选择打开哪个窗口，进入事件循环
 * @param[in] argc  参数个数（包括程序名）
 * @param[in] argv  参数；argv[1] 是窗口名："nvr" 或 "click"，不写默认 "nvr"。
 *                  Qt 会先吃掉它认识的参数（例如 -platform wayland），剩下的留在 argv 里
 * @return app.exec() 的返回值：0 = 正常退出；1 = 参数不认识
 * @note 板子上运行前要设好 XDG_RUNTIME_DIR、WAYLAND_DISPLAY、QT_QPA_PLATFORM，
 *       否则找不到 weston，窗口出不来（run_on_board_with_mac.sh 里已经设了）
 */
int main(int argc, char **argv) {
    // QApplication：每个 Widgets 程序有且只有一个。它负责连上显示服务（这里是 weston），
    // 加载显示插件（QT_QPA_PLATFORM=wayland → plugins/platforms/libqwayland-egl.so）。
    // 构造时会从 argc/argv 里拿走 Qt 自己的参数，所以要在它之后再读 argv[1]
    QApplication app(argc, argv);

    const char *windowName = argc > 1 ? argv[1] : "nvr";
    // strcmp：两个字符串一样返回 0。不能写 windowName == "nvr"，那比的是两个指针（地址）
    if (strcmp(windowName, "nvr") == 0) {
        NvrPreviewWindow nvrPreviewWindow;
        // showFullScreen：铺满整个屏幕，盖住 weston 的任务栏；退出用界面上的「退出」按钮
        nvrPreviewWindow.showFullScreen();
        // exec()：进入事件循环，一直等触摸、鼠标、重绘这些事件并分发，quit() 或窗口关闭后才返回
        return app.exec();
    }
    if (strcmp(windowName, "click") == 0) {
        ClickCounterWindow clickCounterWindow;
        clickCounterWindow.show(); // 控件默认是隐藏的，show() 之后才会显示出来
        return app.exec();
    }
    qWarning("不认识的参数 %s，用法：day7_qt_env [nvr | click]", windowName);
    return 1;
}
