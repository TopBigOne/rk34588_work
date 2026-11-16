//
// NvrPreviewWindow：NVR 预览界面
// 上面是 2×2 的四路画面（VideoTileWidget），下面一排按钮：单画面、四画面、上一路、下一路、退出。
// 点某一格 → 选中这一路（橙色边框）；单画面只显示选中的那一路，四画面切回 2×2。
//
// 布局结构（从外到里）：
//   QVBoxLayout（整个窗口，从上到下）
//     ├─ QGridLayout（2 行 × 2 列的画面格子）
//     ├─ QHBoxLayout（一排按钮，从左到右）
//     └─ 弹簧 addStretch（把剩下的空白都挤到最下面）
//
#pragma once

#include <QWidget>
#include <array>

class QGridLayout;
class VideoTileWidget;

class NvrPreviewWindow : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief 建好四格画面和一排按钮，连好信号槽，默认四画面、选中第 1 路
     * @param[in] parentWidget  父窗口；nullptr = 自己就是顶层窗口
     * @return 无（构造函数）
     * @note 所有子控件、布局都以本窗口为父对象，窗口销毁时 Qt 自动 delete
     */
    explicit NvrPreviewWindow(QWidget *parentWidget = nullptr);

private slots:
    /**
     * @brief 选中某一路：更新 selectedChannelIndex，刷新边框；单画面模式下显示的那一格也跟着换
     * @param[in] requestedChannelIndex  要选中第几路，从 0 开始，取值 0 ~ kChannelCount - 1
     * @return 无
     * @note 点格子（tileClicked 信号）、上一路、下一路都走这里
     */
    void selectChannel(int requestedChannelIndex);

    /**
     * @brief 切到单画面：只显示选中的那一路，它占满整个格子区域的宽度
     * @return 无
     */
    void showSingleChannel();

    /**
     * @brief 切回四画面：四格都显示
     * @return 无
     */
    void showAllChannels();

    /**
     * @brief 选中上一路（第 1 路再往上 → 第 4 路）
     * @return 无
     */
    void selectPreviousChannel();

    /**
     * @brief 选中下一路（第 4 路再往下 → 第 1 路）
     * @return 无
     */
    void selectNextChannel();

private:
    /**
     * @brief 按当前的模式（单画面 / 四画面）和选中的通道，决定每一格显示还是隐藏
     * @return 无
     * @note 隐藏的控件不占布局的位置，剩下的格子会自动重新排
     */
    void updateTileVisibility();

    static constexpr int kChannelCount = 4;     // 一共几路画面
    static constexpr int kGridColumnCount = 2;  // 四画面时每行几格

    std::array<VideoTileWidget *, kChannelCount> videoTiles{}; // 四格画面，父对象是本窗口
    QGridLayout *tileGridLayout = nullptr; // 摆四格画面的网格布局
    int selectedChannelIndex = 0;          // 当前选中第几路，从 0 开始
    bool isSingleChannelMode = false;      // true = 单画面；false = 四画面
};
