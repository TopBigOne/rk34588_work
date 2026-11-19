/**
 * @file
 * @brief 控制栏的实现，布局和显示规则见 control_bar.h
 */
#include "ui/control_bar.h"

#include "common/player_options.h" // seek_by_bytes
#include "player/player_control.h" // toggle_pause、stream_seek
#include "sync/av_sync.h"          // get_master_clock
#include "ui/sdl_context.h"        // renderer、window

/* ---------------- 尺寸（都是窗口短边的比例）---------------- */

static const float kPaddingRatio = 0.03f;          ///< 控制栏四周和元素之间的留白
static const float kProgressRowRatio = 0.08f;      ///< 进度条那一行的高度（也是手指能按中的范围）
static const float kTrackThicknessRatio = 0.012f;  ///< 进度条轨道的粗细
static const float kKnobSizeRatio = 0.045f;        ///< 进度条滑块（方块）的边长
static const float kButtonSizeRatio = 0.12f;       ///< 按钮（正方形）的边长
static const int kGlyphPixelsPerButton = 18;       ///< 点阵字一个“点”的边长 = 按钮边长 / 18

/** 播放中这么久没有操作，控制栏自动隐藏（微秒） */
static const int64_t kAutoHideDelayUs = 4 * 1000000;
/** 松手 seek 以后，这么久之内进度条先显示目标位置，不显示时钟（新位置的帧还没出来时，时钟还是旧位置，滑块会跳回去） */
static const int64_t kSeekDisplayHoldUs = 800 * 1000;

/** 控制栏各部分在窗口里的位置，单位是渲染输出的像素。画和判断点击都用它，保证两边一致 */
typedef struct ControlBarLayout {
    SDL_Rect barRect;            ///< 整个控制栏（半透明黑底）
    SDL_Rect progressHitRect;    ///< 进度条那一行：按在这里面就开始拖
    SDL_Rect trackRect;          ///< 进度条轨道
    SDL_Rect playButtonRect;     ///< 播放 / 暂停按钮
    SDL_Rect exitButtonRect;     ///< 退出按钮
    int timeTextX;               ///< 时间文字左上角
    int timeTextY;
    int glyphPixelSize;          ///< 点阵字一个点的边长
    int knobSize;                ///< 滑块边长
} ControlBarLayout;

/** 点到了控制栏的哪一部分 */
typedef enum ControlBarTarget {
    TARGET_VIDEO,        ///< 控制栏外面（视频画面）
    TARGET_BAR,          ///< 控制栏的空白处
    TARGET_PROGRESS,     ///< 进度条
    TARGET_PLAY_BUTTON,  ///< 播放 / 暂停按钮
    TARGET_EXIT_BUTTON,  ///< 退出按钮
} ControlBarTarget;

/** 控制栏的状态。只在主线程里读写，不用加锁 */
typedef struct ControlBarState {
    int isVisible;                ///< 1 = 显示
    int64_t lastInteractionUs;    ///< 上一次触摸 / 点击的时间（av_gettime_relative，微秒）
    int isPointerDown;            ///< 1 = 手指（或鼠标左键）正按着
    SDL_FingerID trackedFingerId; ///< 正在跟踪的手指（多指时只认第一根）
    int isFingerPointer;          ///< 1 = 当前按着的是手指；0 = 鼠标
    int isPressOnlyRevealing;     ///< 1 = 这一次按下只是把隐藏的控制栏叫出来，抬起时什么都不做
    ControlBarTarget pressTarget; ///< 按下时点在哪：抬起时还在同一个按钮上，才算点击
    int isDraggingProgress;       ///< 1 = 正在拖进度条
    double dragRatio;             ///< 拖动中滑块的位置（占总时长的比例，0～1）
    double seekTargetRatio;       ///< 最近一次松手 seek 的目标比例
    int64_t seekReleasedUs;       ///< 最近一次松手 seek 的时间
    double lastPositionSeconds;   ///< 上一次算出来的播放位置（时钟暂时是 NAN 时用它）
} ControlBarState;

static ControlBarState g_controlBar = { .isVisible = 1 };

/* ---------------- 5×7 点阵字 ---------------- */

/**
 * 每个字 7 行，每行一个字节，低 5 位从左到右是 5 个点（0x10 = 最左边，0x01 = 最右边）。
 * 只需要显示时间，所以只有 0～9、冒号、斜杠、减号（总时长未知时显示 --:--）、空格
 */
static const uint8_t kDigitGlyphs[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, // 2
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, // 3
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
};
static const uint8_t kColonGlyph[7] = { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
static const uint8_t kSlashGlyph[7] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 };
static const uint8_t kMinusGlyph[7] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
static const uint8_t kSpaceGlyph[7] = { 0 };

/** 一个字占 6 个点宽（5 个点 + 1 个点的字间距） */
static const int kGlyphAdvanceDots = 6;

/**
 * @brief 找到一个字符对应的点阵
 * @param[in] character  要画的字符
 * @return 7 个字节的点阵；不认识的字符当空格
 * @note 只支持时间文字里会出现的字符
 */
static const uint8_t *findGlyph(char character) {
    if (character >= '0' && character <= '9') {
        return kDigitGlyphs[character - '0'];
    }
    switch (character) {
        case ':':
            return kColonGlyph;
        case '/':
            return kSlashGlyph;
        case '-':
            return kMinusGlyph;
        default:
            return kSpaceGlyph;
    }
}

/**
 * @brief 用点阵字画一行文字，颜色用调用前 SDL_SetRenderDrawColor 设好的
 * @param[in] text            文字（只支持 findGlyph 认识的字符）
 * @param[in] leftX           左上角 x（像素）
 * @param[in] topY            左上角 y（像素）
 * @param[in] glyphPixelSize  一个点画成多大的正方形（像素）
 * @return 无
 * @note 一个字最多 35 个点，先攒进数组，一次 SDL_RenderFillRects 画完，比一个点调一次快
 */
static void drawDotMatrixText(const char *text, int leftX, int topY, int glyphPixelSize) {
    SDL_Rect dotRects[35];
    for (int charIndex = 0; text[charIndex] != '\0'; charIndex++) {
        const uint8_t *glyph = findGlyph(text[charIndex]);
        const int glyphLeftX = leftX + charIndex * kGlyphAdvanceDots * glyphPixelSize;
        int dotCount = 0;
        for (int row = 0; row < 7; row++) {
            for (int column = 0; column < 5; column++) {
                if (glyph[row] & (0x10 >> column)) { // 0x10 >> column：第 column 个点（从左数）对应的那一位
                    dotRects[dotCount++] = (SDL_Rect){ glyphLeftX + column * glyphPixelSize,
                                                       topY + row * glyphPixelSize, glyphPixelSize, glyphPixelSize };
                }
            }
        }
        if (dotCount > 0) {
            SDL_RenderFillRects(renderer, dotRects, dotCount);
        }
    }
}

/* ---------------- 布局和位置换算 ---------------- */

/**
 * @brief 按渲染输出的大小算出控制栏各部分的位置
 * @param[in]  outputWidth   渲染输出宽（像素）
 * @param[in]  outputHeight  渲染输出高（像素）
 * @param[out] layout        结果
 * @return 无
 * @note 所有尺寸 = 窗口短边 × 比例，横屏、竖屏看起来一样大
 */
static void computeControlBarLayout(int outputWidth, int outputHeight, ControlBarLayout *layout) {
    const float shortSide = (float)FFMIN(outputWidth, outputHeight);
    const int padding = (int)(shortSide * kPaddingRatio);
    const int progressRowHeight = (int)(shortSide * kProgressRowRatio);
    const int trackThickness = FFMAX(2, (int)(shortSide * kTrackThicknessRatio));
    const int buttonSize = (int)(shortSide * kButtonSizeRatio);
    const int barHeight = padding + progressRowHeight + buttonSize + padding;
    const int barTop = outputHeight - barHeight;

    layout->barRect = (SDL_Rect){ 0, barTop, outputWidth, barHeight };
    layout->progressHitRect = (SDL_Rect){ 0, barTop + padding, outputWidth, progressRowHeight };
    layout->trackRect = (SDL_Rect){ padding, barTop + padding + (progressRowHeight - trackThickness) / 2,
                                    outputWidth - 2 * padding, trackThickness };
    const int buttonTop = barTop + padding + progressRowHeight;
    layout->playButtonRect = (SDL_Rect){ padding, buttonTop, buttonSize, buttonSize };
    layout->exitButtonRect = (SDL_Rect){ outputWidth - padding - buttonSize, buttonTop, buttonSize, buttonSize };
    layout->glyphPixelSize = FFMAX(1, buttonSize / kGlyphPixelsPerButton);
    layout->timeTextX = padding + buttonSize + padding;
    layout->timeTextY = buttonTop + (buttonSize - 7 * layout->glyphPixelSize) / 2; // 文字 7 个点高，在按钮那一行里竖直居中
    layout->knobSize = FFMAX(trackThickness * 2, (int)(shortSide * kKnobSizeRatio));
}

/**
 * @brief 读渲染输出的大小，算出布局
 * @param[out] layout  结果
 * @return 1 = 成功；0 = 渲染器还没建好或者大小是 0
 * @note 用 SDL_GetRendererOutputSize（像素）而不是窗口大小（Mac 高分屏上窗口大小是“点”，和像素差一倍）
 */
static int getCurrentLayout(ControlBarLayout *layout) {
    int outputWidth = 0;
    int outputHeight = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) != 0 || outputWidth <= 0
        || outputHeight <= 0) {
        return 0;
    }
    computeControlBarLayout(outputWidth, outputHeight, layout);
    return 1;
}

/**
 * @brief 判断一个点在不在矩形里
 * @param[in] rect  矩形
 * @param[in] x     点的 x（像素）
 * @param[in] y     点的 y（像素）
 * @return 1 = 在；0 = 不在
 * @note 不用 SDL_PointInRect，写出来更直白
 */
static int isPointInRect(const SDL_Rect *rect, int x, int y) {
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

/**
 * @brief 判断一个点落在控制栏的哪一部分
 * @param[in] layout  布局
 * @param[in] x       点的 x（像素）
 * @param[in] y       点的 y（像素）
 * @return 见 ControlBarTarget
 * @note 先判断小的（按钮、进度条），再判断整个控制栏
 */
static ControlBarTarget findTargetAt(const ControlBarLayout *layout, int x, int y) {
    if (isPointInRect(&layout->playButtonRect, x, y)) {
        return TARGET_PLAY_BUTTON;
    }
    if (isPointInRect(&layout->exitButtonRect, x, y)) {
        return TARGET_EXIT_BUTTON;
    }
    if (isPointInRect(&layout->progressHitRect, x, y)) {
        return TARGET_PROGRESS;
    }
    if (isPointInRect(&layout->barRect, x, y)) {
        return TARGET_BAR;
    }
    return TARGET_VIDEO;
}

/**
 * @brief 把横坐标换算成进度条上的比例
 * @param[in] layout  布局
 * @param[in] x       横坐标（像素）
 * @return 0～1；在轨道左边外面算 0，右边外面算 1
 * @note 用轨道（不是整行）的左右边算，滑块和手指对得上
 */
static double convertXToProgressRatio(const ControlBarLayout *layout, int x) {
    const double ratio = (double)(x - layout->trackRect.x) / FFMAX(1, layout->trackRect.w);
    return av_clipd(ratio, 0.0, 1.0);
}

/* ---------------- 播放位置 ---------------- */

/**
 * @brief 片子的总时长
 * @param[in] is  播放器状态
 * @return 总时长（秒）；不知道（直播流等）时返回 0
 * @note ic->duration 的单位是 AV_TIME_BASE（微秒）
 */
static double getDurationSeconds(const VideoState *is) {
    if (!is->ic || is->ic->duration <= 0) {
        return 0.0;
    }
    return is->ic->duration / (double)AV_TIME_BASE;
}

/**
 * @brief 当前播放到第几秒（从片头算起）
 * @param[in] is  播放器状态
 * @return 播放位置（秒）
 * @note 用主时钟（默认是音频时钟）。时钟是 pts，带着文件的起始时间 start_time，要减掉。
 *       刚启动、刚 seek 时时钟是 NAN，这时返回上一次的值
 */
static double getPositionSeconds(VideoState *is) {
    double clockSeconds = get_master_clock(is);
    if (isnan(clockSeconds)) {
        return g_controlBar.lastPositionSeconds;
    }
    if (is->ic && is->ic->start_time != AV_NOPTS_VALUE) {
        clockSeconds -= is->ic->start_time / (double)AV_TIME_BASE;
    }
    g_controlBar.lastPositionSeconds = FFMAX(0.0, clockSeconds);
    return g_controlBar.lastPositionSeconds;
}

/**
 * @brief 跳到整个片子的某个比例处
 * @param[in] is           播放器状态
 * @param[in] targetRatio  目标位置占总时长的比例，0～1
 * @return 无
 * @note 和 ffplay 鼠标右键点窗口跳转的算法一样（event_loop.c 里 SDL_MOUSEBUTTONDOWN 那段）：
 *       目标时间 = 比例 × 总时长 + 文件起始时间；总时长未知时按文件字节数的比例跳
 */
static void seekToDurationRatio(VideoState *is, double targetRatio) {
    if (seek_by_bytes || is->ic->duration <= 0) {
        const int64_t fileSizeBytes = avio_size(is->ic->pb); // 文件总字节数
        stream_seek(is, (int64_t)(fileSizeBytes * targetRatio), 0, 1); // 最后一个参数 1 = 按字节 seek
        return;
    }
    int64_t targetTimestampUs = (int64_t)(targetRatio * is->ic->duration);
    if (is->ic->start_time != AV_NOPTS_VALUE) {
        targetTimestampUs += is->ic->start_time;
    }
    av_log(NULL, AV_LOG_INFO, "进度条跳转到 %.0f%%（%.1f 秒）\n", targetRatio * 100.0, targetTimestampUs / 1000000.0);
    stream_seek(is, targetTimestampUs, 0, 0);
}

/**
 * @brief 把秒数格式化成 MM:SS（一小时以上 H:MM:SS）
 * @param[in]  seconds         秒数；小于 0 时输出 --:--
 * @param[in]  useHours        1 = 按 H:MM:SS 输出（总时长超过一小时时，两边统一用这种格式）
 * @param[out] text            输出缓冲区
 * @param[in]  textSizeBytes   缓冲区大小
 * @return 无
 * @note 秒数向下取整：播到 59.9 秒显示 00:59
 */
static void formatTime(double seconds, int useHours, char *text, size_t textSizeBytes) {
    if (seconds < 0.0) {
        snprintf(text, textSizeBytes, useHours ? "-:--:--" : "--:--");
        return;
    }
    const int totalSeconds = (int)seconds;
    if (useHours) {
        snprintf(text, textSizeBytes, "%d:%02d:%02d", totalSeconds / 3600, totalSeconds / 60 % 60, totalSeconds % 60);
    } else {
        snprintf(text, textSizeBytes, "%02d:%02d", totalSeconds / 60, totalSeconds % 60);
    }
}

/* ---------------- 画 ---------------- */

/**
 * @brief 画播放（三角）或暂停（两竖条）图标
 * @param[in] buttonRect  按钮的位置
 * @param[in] isPaused    1 = 现在是暂停状态，画“播放”三角（点了就播放）；0 = 画“暂停”两竖条
 * @return 无
 * @note 三角用一行一行的横线拼出来（SDL 2.0.22 没有直接画三角形的老接口，SDL_RenderGeometry 2.0.18 才有，这里不依赖它）
 */
static void drawPlayPauseIcon(const SDL_Rect *buttonRect, int isPaused) {
    const int iconInset = buttonRect->w / 4; // 图标离按钮边缘留 1/4
    const int iconLeft = buttonRect->x + iconInset;
    const int iconTop = buttonRect->y + iconInset;
    const int iconSize = buttonRect->w - 2 * iconInset;
    if (isPaused) {
        // 尖朝右的三角：第 row 行的宽度从两头往中间线性变宽，中间一行最宽（= iconSize）
        for (int row = 0; row < iconSize; row++) {
            const int distanceFromCenter = abs(row - iconSize / 2);
            const int rowWidth = iconSize - distanceFromCenter * 2;
            if (rowWidth > 0) {
                SDL_RenderDrawLine(renderer, iconLeft, iconTop + row, iconLeft + rowWidth - 1, iconTop + row);
            }
        }
    } else {
        const int barWidth = iconSize / 3;
        const SDL_Rect pauseBars[2] = {
            { iconLeft, iconTop, barWidth, iconSize },
            { iconLeft + iconSize - barWidth, iconTop, barWidth, iconSize },
        };
        SDL_RenderFillRects(renderer, pauseBars, 2);
    }
}

/**
 * @brief 画退出图标（✕）
 * @param[in] buttonRect  按钮的位置
 * @return 无
 * @note 两条对角线，每条用一串小方块沿对角线排出来，方块边长就是线的粗细
 */
static void drawExitIcon(const SDL_Rect *buttonRect) {
    const int iconInset = buttonRect->w / 4;
    const int iconLeft = buttonRect->x + iconInset;
    const int iconTop = buttonRect->y + iconInset;
    const int iconSize = buttonRect->w - 2 * iconInset;
    const int lineThickness = FFMAX(2, iconSize / 7);
    for (int step = 0; step <= iconSize - lineThickness; step++) {
        const SDL_Rect strokeRects[2] = {
            { iconLeft + step, iconTop + step, lineThickness, lineThickness },                            // 左上 → 右下
            { iconLeft + iconSize - lineThickness - step, iconTop + step, lineThickness, lineThickness }, // 右上 → 左下
        };
        SDL_RenderFillRects(renderer, strokeRects, 2);
    }
}

void drawControlBar(VideoState *is) {
    ControlBarLayout layout;
    if (!g_controlBar.isVisible || !getCurrentLayout(&layout)) {
        return;
    }

    // 第 1 步 算进度：拖动中显示手指位置；刚松手的一小会儿显示目标位置；其他时候显示时钟
    const double durationSeconds = getDurationSeconds(is);
    double positionSeconds = getPositionSeconds(is);
    double progressRatio = durationSeconds > 0.0 ? positionSeconds / durationSeconds : 0.0;
    if (g_controlBar.isDraggingProgress) {
        progressRatio = g_controlBar.dragRatio;
    } else if (av_gettime_relative() - g_controlBar.seekReleasedUs < kSeekDisplayHoldUs) {
        progressRatio = g_controlBar.seekTargetRatio;
    }
    progressRatio = av_clipd(progressRatio, 0.0, 1.0);
    if (durationSeconds > 0.0 && (g_controlBar.isDraggingProgress
                                  || av_gettime_relative() - g_controlBar.seekReleasedUs < kSeekDisplayHoldUs)) {
        positionSeconds = progressRatio * durationSeconds; // 时间文字也跟着滑块走
    }

    // 第 2 步 半透明黑底。BLEND：按 alpha 和下面的视频混合（160/255 ≈ 63% 不透明）；画完把混合方式改回去
    SDL_BlendMode previousBlendMode;
    SDL_GetRenderDrawBlendMode(renderer, &previousBlendMode);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &layout.barRect);

    // 第 3 步 进度条：整条轨道（浅灰）→ 已播放部分（青绿，和图标一个色系）→ 滑块（白）
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 90);
    SDL_RenderFillRect(renderer, &layout.trackRect);
    const int playedWidth = (int)(layout.trackRect.w * progressRatio);
    const SDL_Rect playedRect = { layout.trackRect.x, layout.trackRect.y, playedWidth, layout.trackRect.h };
    SDL_SetRenderDrawColor(renderer, 40, 200, 170, 255);
    SDL_RenderFillRect(renderer, &playedRect);
    const int knobCenterX = layout.trackRect.x + playedWidth;
    const int knobCenterY = layout.trackRect.y + layout.trackRect.h / 2;
    const SDL_Rect knobRect = { knobCenterX - layout.knobSize / 2, knobCenterY - layout.knobSize / 2, layout.knobSize,
                                layout.knobSize };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &knobRect);

    // 第 4 步 按钮和时间文字（白色）
    drawPlayPauseIcon(&layout.playButtonRect, is->paused);
    drawExitIcon(&layout.exitButtonRect);
    const int useHours = durationSeconds >= 3600.0;
    char positionText[16];
    char durationText[16];
    char timeText[40];
    formatTime(positionSeconds, useHours, positionText, sizeof(positionText));
    formatTime(durationSeconds > 0.0 ? durationSeconds : -1.0, useHours, durationText, sizeof(durationText));
    snprintf(timeText, sizeof(timeText), "%s / %s", positionText, durationText);
    drawDotMatrixText(timeText, layout.timeTextX, layout.timeTextY, layout.glyphPixelSize);

    SDL_SetRenderDrawBlendMode(renderer, previousBlendMode);
}

/* ---------------- 显示 / 隐藏 ---------------- */

/**
 * @brief 设置控制栏显示与否，变了就让画面重画
 * @param[in] is         播放器状态
 * @param[in] isVisible  1 = 显示；0 = 隐藏
 * @return 无
 * @note force_refresh：暂停时 ffplay 不会自己重画，要靠它
 */
static void setControlBarVisible(VideoState *is, int isVisible) {
    if (g_controlBar.isVisible != isVisible) {
        g_controlBar.isVisible = isVisible;
        is->force_refresh = 1;
    }
}

void updateControlBarVisibility(VideoState *is) {
    if (g_controlBar.lastInteractionUs == 0) {
        // 第一次调用：从现在开始算 4 秒。不设的话 0 离现在（开机以来的微秒数）早就超过 4 秒，控制栏一启动就被藏掉
        g_controlBar.lastInteractionUs = av_gettime_relative();
    }
    if (is->paused) {
        setControlBarVisible(is, 1); // 暂停时一直显示（按键盘空格暂停的情况也一样）
        return;
    }
    if (g_controlBar.isVisible && !g_controlBar.isPointerDown
        && av_gettime_relative() - g_controlBar.lastInteractionUs > kAutoHideDelayUs) {
        setControlBarVisible(is, 0);
    }
}

/* ---------------- 触摸、鼠标 ---------------- */

/** 统一后的指针动作：手指和鼠标左键都换算成这三种 */
typedef enum PointerAction {
    POINTER_NONE,
    POINTER_DOWN,
    POINTER_MOVE,
    POINTER_UP,
} PointerAction;

/**
 * @brief 把 SDL 的触摸 / 鼠标事件换算成“按下 / 移动 / 抬起 + 像素坐标”
 * @param[in]  event   SDL 事件
 * @param[in]  layout  布局（只用来知道渲染输出多大）
 * @param[out] x       像素坐标 x
 * @param[out] y       像素坐标 y
 * @return 动作；控制栏不管的事件返回 POINTER_NONE
 * @note 触摸事件的坐标是 0～1 的比例，乘上输出大小；鼠标事件的坐标是窗口坐标（“点”），按输出 / 窗口的比例换成像素。
 *       触摸模拟出来的鼠标事件（which == SDL_TOUCH_MOUSEID）不要，否则一次点击会处理两遍。
 *       多根手指时只认第一根
 */
static PointerAction convertEventToPointer(const SDL_Event *event, const ControlBarLayout *layout, int *x, int *y) {
    const int outputWidth = layout->barRect.w;
    const int outputHeight = layout->barRect.y + layout->barRect.h;
    switch (event->type) {
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            const SDL_TouchFingerEvent *fingerEvent = &event->tfinger;
            if (event->type == SDL_FINGERDOWN) {
                if (g_controlBar.isPointerDown) {
                    return POINTER_NONE; // 已经有一根手指按着
                }
                g_controlBar.trackedFingerId = fingerEvent->fingerId;
            } else if (!g_controlBar.isPointerDown || !g_controlBar.isFingerPointer
                       || fingerEvent->fingerId != g_controlBar.trackedFingerId) {
                return POINTER_NONE;
            }
            g_controlBar.isFingerPointer = 1;
            *x = (int)(fingerEvent->x * outputWidth);
            *y = (int)(fingerEvent->y * outputHeight);
            return event->type == SDL_FINGERDOWN ? POINTER_DOWN
                   : event->type == SDL_FINGERUP ? POINTER_UP : POINTER_MOVE;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION: {
            int windowWidth = 0;
            int windowHeight = 0;
            SDL_GetWindowSize(window, &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                return POINTER_NONE;
            }
            if (event->type == SDL_MOUSEMOTION) {
                if (event->motion.which == SDL_TOUCH_MOUSEID || !g_controlBar.isPointerDown || g_controlBar.isFingerPointer) {
                    return POINTER_NONE; // 没按着左键的鼠标移动交给 ffplay（显示鼠标指针）
                }
                *x = event->motion.x * outputWidth / windowWidth;
                *y = event->motion.y * outputHeight / windowHeight;
                return POINTER_MOVE;
            }
            if (event->button.which == SDL_TOUCH_MOUSEID || event->button.button != SDL_BUTTON_LEFT) {
                return POINTER_NONE; // 右键交给 ffplay（右键点窗口跳转）
            }
            if (event->type == SDL_MOUSEBUTTONDOWN && g_controlBar.isPointerDown) {
                return POINTER_NONE;
            }
            if (event->type == SDL_MOUSEBUTTONUP && (!g_controlBar.isPointerDown || g_controlBar.isFingerPointer)) {
                return POINTER_NONE;
            }
            g_controlBar.isFingerPointer = 0;
            *x = event->button.x * outputWidth / windowWidth;
            *y = event->button.y * outputHeight / windowHeight;
            return event->type == SDL_MOUSEBUTTONDOWN ? POINTER_DOWN : POINTER_UP;
        }
        default:
            return POINTER_NONE;
    }
}

ControlBarEventResult handleControlBarEvent(VideoState *is, const SDL_Event *event) {
    ControlBarLayout layout;
    if (!getCurrentLayout(&layout)) {
        return CONTROL_BAR_EVENT_NOT_HANDLED;
    }
    int x = 0;
    int y = 0;
    const PointerAction action = convertEventToPointer(event, &layout, &x, &y);
    if (action == POINTER_NONE) {
        return CONTROL_BAR_EVENT_NOT_HANDLED;
    }
    g_controlBar.lastInteractionUs = av_gettime_relative();
    // 窗口还没按视频大小建好时（read_thread 还在打开文件）不处理，和 ffplay 的按键处理一样
    if (!is->width) {
        return CONTROL_BAR_EVENT_HANDLED;
    }

    switch (action) {
        case POINTER_DOWN:
            g_controlBar.isPointerDown = 1;
            // 隐藏时第一下只把控制栏叫出来，不触发下面的按钮（用户看不见按钮在哪）
            if (!g_controlBar.isVisible) {
                g_controlBar.isPressOnlyRevealing = 1;
                setControlBarVisible(is, 1);
                return CONTROL_BAR_EVENT_HANDLED;
            }
            g_controlBar.isPressOnlyRevealing = 0;
            g_controlBar.pressTarget = findTargetAt(&layout, x, y);
            if (g_controlBar.pressTarget == TARGET_PROGRESS) {
                g_controlBar.isDraggingProgress = 1;
                g_controlBar.dragRatio = convertXToProgressRatio(&layout, x);
                is->force_refresh = 1;
            }
            return CONTROL_BAR_EVENT_HANDLED;

        case POINTER_MOVE:
            if (g_controlBar.isDraggingProgress) {
                g_controlBar.dragRatio = convertXToProgressRatio(&layout, x);
                is->force_refresh = 1; // 暂停时拖动也要重画滑块
            }
            return CONTROL_BAR_EVENT_HANDLED;

        case POINTER_UP: {
            g_controlBar.isPointerDown = 0;
            if (g_controlBar.isDraggingProgress) {
                // 松手才 seek 一次：拖动过程中不 seek，免得一直清队列、reset 解码器
                g_controlBar.isDraggingProgress = 0;
                g_controlBar.dragRatio = convertXToProgressRatio(&layout, x);
                g_controlBar.seekTargetRatio = g_controlBar.dragRatio;
                g_controlBar.seekReleasedUs = av_gettime_relative();
                seekToDurationRatio(is, g_controlBar.dragRatio);
                is->force_refresh = 1;
                return CONTROL_BAR_EVENT_HANDLED;
            }
            if (g_controlBar.isPressOnlyRevealing) {
                g_controlBar.isPressOnlyRevealing = 0;
                return CONTROL_BAR_EVENT_HANDLED;
            }
            // 抬起时还在按下时的那个东西上，才算点击（按下后手指滑走，就当没点）
            const ControlBarTarget releaseTarget = findTargetAt(&layout, x, y);
            if (releaseTarget != g_controlBar.pressTarget) {
                return CONTROL_BAR_EVENT_HANDLED;
            }
            switch (releaseTarget) {
                case TARGET_PLAY_BUTTON:
                    toggle_pause(is);
                    is->force_refresh = 1; // 图标要从“暂停”变成“播放”，暂停后 ffplay 不会自己重画
                    break;
                case TARGET_EXIT_BUTTON:
                    return CONTROL_BAR_EVENT_QUIT_REQUESTED;
                case TARGET_VIDEO:
                    if (!is->paused) {
                        setControlBarVisible(is, 0); // 点画面：隐藏控制栏（暂停时保持显示）
                    }
                    break;
                default:
                    break; // 点在控制栏空白处：什么都不做
            }
            return CONTROL_BAR_EVENT_HANDLED;
        }

        default:
            return CONTROL_BAR_EVENT_NOT_HANDLED;
    }
}
