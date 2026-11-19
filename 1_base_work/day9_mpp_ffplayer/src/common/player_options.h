#pragma once

/**
 * @file
 * @brief 用户通过命令行设置的选项（全局变量）
 * @note 原版 ffplay.c 里是一堆 static 全局变量；拆分后各模块都要读，所以在 player_options.c 里定义、这里 extern。
 *       命令行解析表（OptionDef options[]）在 app/ffplay_options.c，它直接写这些变量
 */

#include "common/ffplay_common.h"

/** sws_scale 的缩放算法，upload_texture 里不支持的像素格式转 BGRA 时用 */
extern unsigned sws_flags;

/* options specified by the user */
extern const AVInputFormat *file_iformat;      ///< -f 强制输入格式
extern const char *input_filename;             ///< 输入文件 / URL
extern const char *window_title;               ///< -window_title
extern int default_width;                      ///< 窗口默认宽（没有视频时）
extern int default_height;                     ///< 窗口默认高
extern int screen_width;                       ///< -x 强制窗口宽
extern int screen_height;                      ///< -y 强制窗口高
extern int screen_left;                        ///< -left 窗口 x 坐标
extern int screen_top;                         ///< -top 窗口 y 坐标
extern int audio_disable;                      ///< -an
extern int video_disable;                      ///< -vn
extern int subtitle_disable;                   ///< -sn
extern const char *wanted_stream_spec[AVMEDIA_TYPE_NB]; ///< -ast / -vst / -sst
extern int seek_by_bytes;                      ///< -bytes：按字节 seek（-1 = 自动）
extern float seek_interval;                    ///< 左右方向键 seek 的秒数
extern int display_disable;                    ///< -nodisp
extern int borderless;                         ///< -noborder
extern int alwaysontop;                        ///< -alwaysontop
extern int startup_volume;                     ///< -volume 0~100
extern int show_status;                        ///< -stats：控制台打印 A-V 差值等状态
extern int av_sync_type;                       ///< -sync audio/video/ext
extern int64_t start_time;                     ///< -ss 起播位置（微秒）
extern int64_t duration;                       ///< -t 播放时长（微秒）
extern int fast;                               ///< -fast
extern int genpts;                             ///< -genpts
extern int lowres;                             ///< -lowres
extern int decoder_reorder_pts;                ///< -drp
extern int autoexit;                           ///< -autoexit 播完退出
extern int exit_on_keydown;                    ///< -exitonkeydown
extern int exit_on_mousedown;                  ///< -exitonmousedown
extern int loop;                               ///< -loop 循环次数（0 = 无限）
extern int framedrop;                          ///< -framedrop 视频落后时丢帧（-1 = 自动）
extern int infinite_buffer;                    ///< -infbuf 不限制缓存大小
extern enum ShowMode show_mode;                ///< -showmode
extern const char *audio_codec_name;           ///< -acodec
extern const char *subtitle_codec_name;        ///< -scodec
extern const char *video_codec_name;           ///< -vcodec
extern double rdftspeed;                       ///< -rdftspeed 频谱/波形刷新间隔（秒）
#if CONFIG_AVFILTER
extern const char **vfilters_list;             ///< -vf 可多次指定，按 w 键循环切换
extern int nb_vfilters;
extern char *afilters;                         ///< -af
#endif
extern int autorotate;                         ///< -autorotate 按 displaymatrix 自动旋转
extern int find_stream_info;                   ///< -find_stream_info
extern int filter_nbthreads;                   ///< -filter_threads
extern int mpp_decode_disable;                 ///< MppPlayer 新增：-nomppdec 不用 MPP 硬解，视频走 FFmpeg 软解（对比用）
