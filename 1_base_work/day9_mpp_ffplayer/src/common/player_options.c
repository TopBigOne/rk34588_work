/*
 * 本文件拆自 FFmpeg 6.0 fftools/ffplay.c（Copyright (c) 2003 Fabrice Bellard），
 * 遵循 GNU LGPL 2.1 或更高版本。
 */
#include "common/player_options.h"

unsigned sws_flags = SWS_BICUBIC;

/* options specified by the user */
const AVInputFormat *file_iformat;
const char *input_filename;
const char *window_title;
int default_width  = 640;
int default_height = 480;
int screen_width  = 0;
int screen_height = 0;
int screen_left = SDL_WINDOWPOS_CENTERED;
int screen_top = SDL_WINDOWPOS_CENTERED;
int audio_disable;
int video_disable;
int subtitle_disable;
const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
int seek_by_bytes = -1;
float seek_interval = 10;
int display_disable;
int borderless;
int alwaysontop;
int startup_volume = 10; // MppPlayer：默认音量 10%（原版 100）。板载喇叭 100% 太响，2026-11-19 用户定；命令行 -volume N 仍可改
int show_status = -1;
int av_sync_type = AV_SYNC_AUDIO_MASTER;
int64_t start_time = AV_NOPTS_VALUE;
int64_t duration = AV_NOPTS_VALUE;
int fast = 0;
int genpts = 0;
int lowres = 0;
int decoder_reorder_pts = -1;
int autoexit;
int exit_on_keydown;
int exit_on_mousedown;
int loop = 1;
int framedrop = -1;
int infinite_buffer = -1;
enum ShowMode show_mode = SHOW_MODE_NONE;
const char *audio_codec_name;
const char *subtitle_codec_name;
const char *video_codec_name;
double rdftspeed = 0.02;
#if CONFIG_AVFILTER
const char **vfilters_list = NULL;
int nb_vfilters = 0;
char *afilters = NULL;
#endif
int autorotate = 1;
int find_stream_info = 1;
int filter_nbthreads = 0;
int mpp_decode_disable = 0;
