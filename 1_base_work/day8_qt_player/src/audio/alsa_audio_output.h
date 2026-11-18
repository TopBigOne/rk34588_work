#pragma once

#include <alsa/asoundlib.h>
#include <cstdint> // int16_t、int64_t
#include <memory>  // std::unique_ptr

/** SndPcmPtr 的删除器 */
struct SndPcmCloser {
    /**
     * @brief 关闭 PCM 设备
     * @param[in] pcm  snd_pcm_open 打开的设备
     * @return 无
     * @note 先 snd_pcm_drop：立刻停止，缓冲区里没播完的声音直接丢掉；再 snd_pcm_close 关设备。
     *       直接操作硬件声卡时 snd_pcm_close 自己就会丢掉缓冲区，但板子上的 "default" 走 PulseAudio 插件，
     *       这一点没实测过，所以显式 drop 一次，保证点了退出声音马上停
     */
    void operator()(snd_pcm_t *pcm) const {
        snd_pcm_drop(pcm);
        snd_pcm_close(pcm);
    }
};
using SndPcmPtr = std::unique_ptr<snd_pcm_t, SndPcmCloser>;

/**
 * @brief ALSA 声音输出：打开声卡，把 S16 交错 PCM 写进去播出来；支持暂停、清空、查"还有多少没播"
 * @note ALSA 是 Linux 内核的声卡接口，作用相当于 Android 的 AAudio / OpenSL ES：只管把 PCM 交给声卡，不管解码。
 *       声卡里有一个环形缓冲区（这里设成约 100 ms），snd_pcm_writei 往里写，声卡按采样率匀速往外取着播；
 *       缓冲区满了 writei 就阻塞，所以写的线程自然被声卡"牵着走"，这就是音频能当时钟的原因
 */
class AlsaAudioOutput {
public:
    AlsaAudioOutput() = default;
    AlsaAudioOutput(const AlsaAudioOutput &) = delete;            // 不能复制：同一个设备不能关两次
    AlsaAudioOutput &operator=(const AlsaAudioOutput &) = delete; // 同上

    /**
     * @brief 打开声卡，设置格式
     * @param[in] deviceName           ALSA 设备名，例如 "default"、"plughw:0,0"（第 0 张卡的第 0 个设备，带自动格式转换）
     * @param[in] requestedSampleRate  采样率，Hz，例如 48000
     * @param[in] requestedChannelCount 声道数，1 或 2
     * @param[in] bufferDurationUs     声卡缓冲区多长，微秒。越长越不容易断音，但暂停、seek 的反应越慢
     * @return true = 成功；false = 失败（已打印原因）
     * @note 采样格式固定 S16_LE 交错。只能调用一次
     */
    bool open(const char *deviceName, int requestedSampleRate, int requestedChannelCount, int bufferDurationUs);

    /**
     * @brief 写一段 PCM，全部写进去才返回（缓冲区满了就阻塞等）
     * @param[in] samples     S16 交错采样（左右左右……）
     * @param[in] frameCount  帧数（一帧 = 每个声道各一个采样）
     * @return true = 写完了；false = 出错（已打印原因）
     * @note 遇到 underrun（写得太慢，声卡把缓冲区播空了，返回 -EPIPE）会自动恢复后接着写
     */
    bool write(const int16_t *samples, int frameCount);

    /**
     * @brief 声卡里还有多少声音没播出去
     * @return 微秒；出错或者已经播空时返回 0
     * @note 用 snd_pcm_delay 查：从现在写进去的下一个采样，到它从喇叭里响出来，要等多少帧
     */
    int64_t getQueuedDurationUs();

    /**
     * @brief 暂停 / 继续
     * @param[in] shouldPause  true = 暂停；false = 继续
     * @return 无
     * @note 声卡支持暂停时用 snd_pcm_pause（缓冲区里的声音保留，继续时接着播）；
     *       不支持时暂停用 snd_pcm_drop（丢掉缓冲区里的声音，约 100 ms），继续时 snd_pcm_prepare
     */
    void setPaused(bool shouldPause);

    /**
     * @brief 丢掉缓冲区里还没播的声音，准备接着写（seek 时调用）
     * @return 无
     * @note snd_pcm_drop 立刻停止并清空，snd_pcm_prepare 让设备回到"可以写"的状态
     */
    void dropQueued();

    /**
     * @brief 采样率
     * @return Hz
     * @note open 之后才有意义
     */
    int getSampleRate() const { return sampleRate; }

private:
    SndPcmPtr pcmPtr;          // 打开的 PCM 设备
    int sampleRate = 0;        // 采样率，Hz
    int channelCount = 0;      // 声道数
    bool canPause = false;     // 声卡支不支持 snd_pcm_pause
    bool isPaused = false;     // 现在是不是暂停着
};
