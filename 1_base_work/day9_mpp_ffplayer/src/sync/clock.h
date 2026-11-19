#pragma once

/**
 * @file
 * @brief 播放时钟：记录"某个时刻播到了哪个 pts"，之后按真实时间流逝推算当前 pts
 * @note 当前 pts = pts_drift + 现在时间 - (现在时间 - last_updated) * (1 - speed)。
 *       音频、视频、外部各一个时钟（audclk / vidclk / extclk）
 */

#include "common/ffplay_common.h"

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/**
 * @brief 读时钟当前值（秒）
 * @return 当前 pts；时钟已过期（serial 不等于队列 serial）时返回 NAN
 */
double get_clock(Clock *c);

/**
 * @brief 在指定系统时间点设置时钟
 * @param[in] c       时钟
 * @param[in] pts     该时刻的 pts（秒）
 * @param[in] serial  pts 所属的 serial
 * @param[in] time    系统时间（秒）
 */
void set_clock_at(Clock *c, double pts, int serial, double time);

/**
 * @brief 以当前系统时间设置时钟
 */
void set_clock(Clock *c, double pts, int serial);

/**
 * @brief 改变时钟速度（外部时钟按缓存水位微调用）
 */
void set_clock_speed(Clock *c, double speed);

/**
 * @brief 初始化时钟：速度 1.0，pts 为 NAN
 * @param[in] c             时钟
 * @param[in] queue_serial  关联的包队列 serial 地址，用于判断时钟是否过期
 */
void init_clock(Clock *c, int *queue_serial);

/**
 * @brief 如果 c 无效或和 slave 相差超过 AV_NOSYNC_THRESHOLD，就把 c 拨到 slave 的值
 */
void sync_clock_to_slave(Clock *c, Clock *slave);
