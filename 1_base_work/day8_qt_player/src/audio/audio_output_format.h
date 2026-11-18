/*
 * 声音输出的固定格式：所有音频最后都转成这个格式交给 ALSA
 */
#pragma once

#include <cstdlib> // getenv

constexpr int kOutputSampleRate = 48000;       // 输出采样率，Hz（视频文件里最常见的就是 48000）
constexpr int kOutputChannelCount = 2;         // 输出声道数：双声道
constexpr int kAlsaBufferDurationUs = 100000;  // 声卡缓冲区 100 ms：够防断音，暂停、seek 时最多晚 0.1 秒停下来

/**
 * @brief 用哪个 ALSA 设备播放
 * @return 环境变量 PLAYER_ALSA_DEVICE 的值；没设置时返回 "default"
 * @note "default" 是 ALSA 的默认设备（一般是第 0 张声卡，带自动格式转换）。
 *       板子有好几张声卡时（HDMI、耳机……），用 aplay -l 看卡号，再设
 *       PLAYER_ALSA_DEVICE=plughw:卡号,0 指定
 */
inline const char *getAlsaDeviceName() {
    const char *deviceName = std::getenv("PLAYER_ALSA_DEVICE");
    return deviceName ? deviceName : "default";
}
