/*
 * 本文件拆自 FFmpeg 6.0 fftools/ffplay.c（Copyright (c) 2003 Fabrice Bellard），
 * 遵循 GNU LGPL 2.1 或更高版本。
 */
#include "ui/sdl_context.h"

/* current context */
int is_full_screen;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_RendererInfo renderer_info = {0};
SDL_AudioDeviceID audio_dev;
