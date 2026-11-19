#pragma once

/**
 * @file
 * @brief FFmpeg 像素格式 ↔ SDL 纹理格式 对照表
 * @note 用 static const 定义在头文件里：video_render.c（选纹理格式）和 av_filters.c（限定滤镜输出格式）
 *       都要对它做 FF_ARRAY_ELEMS，需要完整的数组类型，所以每个使用者各拿一份只读副本
 */

#include "common/ffplay_common.h"

static const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int texture_fmt;
} sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
    { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
    { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
    // MppPlayer 新增：MPP 硬解输出 NV12。不加这一行的话，滤镜会在 CPU 上用 swscale 把 NV12 转成 YUV420P，
    // 1080p 每帧都要转一次；加上以后 NV12 原样上传，颜色转换交给 GPU（SDL 的 GLES2 渲染器支持 NV12 纹理）
    { AV_PIX_FMT_NV12,           SDL_PIXELFORMAT_NV12 },
    { AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
};
