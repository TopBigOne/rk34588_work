#pragma once

/**
 * @file
 * @brief 视频渲染：AVFrame 上传成 SDL 纹理、计算显示区域、画视频帧和字幕、打开窗口
 */

#include "player/video_state.h"

/**
 * @brief 纹理不存在或格式 / 尺寸变了就重建
 * @param[in,out] texture       纹理
 * @param[in]     new_format    SDL 像素格式
 * @param[in]     new_width     宽
 * @param[in]     new_height    高
 * @param[in]     blendmode     混合模式
 * @param[in]     init_texture  1 = 新建后清零
 * @return 0 = 成功；-1 = 失败
 */
int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);

/**
 * @brief 按宽高比（SAR）把画面居中放进屏幕区域
 * @param[out] rect                 计算结果
 * @param[in]  scr_xleft, scr_ytop  可用区域左上角
 * @param[in]  scr_width, scr_height 可用区域大小
 * @param[in]  pic_width, pic_height 画面像素尺寸
 * @param[in]  pic_sar              画面的采样宽高比
 */
void calculate_display_rect(SDL_Rect *rect,
                            int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                            int pic_width, int pic_height, AVRational pic_sar);

/**
 * @brief 设置 SDL 的 YUV→RGB 转换矩阵（BT.601 / BT.709 / JPEG）
 */
void set_sdl_yuv_conversion_mode(AVFrame *frame);

/**
 * @brief 画当前视频帧（及字幕）
 */
void video_image_display(VideoState *is);

/**
 * @brief 根据视频尺寸和 -x/-y 设置默认窗口大小
 * @param[in] width, height  视频尺寸
 * @param[in] sar            采样宽高比
 */
void set_default_window_size(int width, int height, AVRational sar);

/**
 * @brief 设置窗口标题、大小、位置并显示窗口
 * @return 0
 */
int video_open(VideoState *is);

/* display the current picture, if any */
/**
 * @brief 清屏 → 画视频或音频波形 → present
 */
void video_display(VideoState *is);
