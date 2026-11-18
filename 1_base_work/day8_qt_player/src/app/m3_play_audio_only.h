#pragma once

/**
 * @brief M3：mp4 → 拆封装 → 软解音频 → 转成 S16 交错 48000 Hz 双声道 → ALSA 播放（可以同时存成 PCM 文件）
 * @param[in] inputPath      mp4 文件路径
 * @param[in] pcmOutputPath  同时把 PCM 存到这个文件；nullptr = 不存
 * @return 0 = 成功；1 = 失败
 * @note 没有画面，也不用线程：写声卡会阻塞，所以按真实时间的速度播放。
 *       PCM 文件可以和 Mac 上 ffmpeg -i in.mp4 -vn -f s16le -ar 48000 -ac 2 out.pcm 的结果比 md5
 */
int runM3PlayAudioOnly(const char *inputPath, const char *pcmOutputPath);
