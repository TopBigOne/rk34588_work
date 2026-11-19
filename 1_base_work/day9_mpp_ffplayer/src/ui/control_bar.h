#pragma once

/**
 * @file
 * @brief MppPlayer 新增：画在视频上面的控制栏（进度条、播放时间 / 总时间、播放 / 暂停按钮、退出按钮）
 * @note ffplay 原版没有任何界面元素，全靠键盘；板子只有触摸屏，所以自己用 SDL 画一个。
 *
 *       布局（窗口底部，半透明黑底，两行）：
 *
 *         ┌───────────────────────────────────────────┐
 *         │                                           │
 *         │                 视频画面                    │  点画面：显示 / 隐藏控制栏
 *         │                                           │
 *         ├───────────────────────────────────────────┤
 *         │  ━━━━━━━━━━━━━━━●─────────────────────────  │  第 1 行：进度条，按住拖动，松手跳过去
 *         │  [▶/❚❚]  01:23 / 04:56                [✕]  │  第 2 行：播放/暂停、时间、退出
 *         └───────────────────────────────────────────┘
 *
 *       所有尺寸按窗口短边等比缩放（竖屏 1080×1920 时按钮约 130 像素），横屏、竖屏、小窗口都能用。
 *       时间数字用内置的 5×7 点阵字画（板子上没有可用的字体文件，也不额外依赖 SDL_ttf）。
 *       只用 SDL_RenderFillRect(s) 画矩形，板子上的 SDL 2.0.22 都支持。
 *
 *       显示规则：启动时显示；播放中 4 秒没有操作自动隐藏；暂停时一直显示；
 *       隐藏时第一下点击只用来把控制栏叫出来，不触发按钮。
 */

#include "player/video_state.h"

/** handleControlBarEvent 的处理结果 */
typedef enum ControlBarEventResult {
    CONTROL_BAR_EVENT_NOT_HANDLED = 0, ///< 控制栏不管这个事件，交给 ffplay 原来的处理（键盘、鼠标右键 seek 等）
    CONTROL_BAR_EVENT_HANDLED,         ///< 控制栏处理掉了，ffplay 原来的处理不要再管
    CONTROL_BAR_EVENT_QUIT_REQUESTED,  ///< 用户点了退出按钮，调用方要 do_exit
} ControlBarEventResult;

/**
 * @brief 处理触摸和鼠标左键事件：拖进度条、点按钮、点画面显示 / 隐藏控制栏
 * @param[in] is     播放器状态
 * @param[in] event  SDL 事件。处理 SDL_FINGERDOWN / SDL_FINGERMOTION / SDL_FINGERUP，
 *                   以及真鼠标（不是触摸模拟出来的）的左键按下 / 抬起 / 按住移动
 * @return 见 ControlBarEventResult
 * @note 在主线程（event_loop）里调用。状态变了会设 is->force_refresh，让暂停时也重画
 */
ControlBarEventResult handleControlBarEvent(VideoState *is, const SDL_Event *event);

/**
 * @brief 检查要不要自动隐藏控制栏（播放中超过 4 秒没有操作），暂停时把它重新显示出来
 * @param[in] is  播放器状态
 * @return 无
 * @note 在主线程的刷新循环（refresh_loop_wait_event）里每轮调用一次，开销只是读一次时间
 */
void updateControlBarVisibility(VideoState *is);

/**
 * @brief 把控制栏画到当前渲染目标上
 * @param[in] is  播放器状态（读当前播放位置、总时长、是否暂停）
 * @return 无
 * @note 在 video_display 里画完视频之后、SDL_RenderPresent 之前调用；控制栏隐藏时什么都不画
 */
void drawControlBar(VideoState *is);
