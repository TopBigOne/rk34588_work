#include "app/m3_play_audio_only.h"

#include <cstdio> // fopen、fwrite
#include <vector>

#include "audio/alsa_audio_output.h"
#include "audio/audio_output_format.h"
#include "audio/audio_resampler.h"
#include "audio/software_audio_decoder.h"
#include "common/ffmpeg_utils.h"
#include "common/log.h"
#include "common/mpp_utils.h" // FilePtr
#include "common/time_utils.h"
#include "demux/mp4_demuxer.h"

static const char *const kTag = "M3";

/**
 * @brief 把解码器里现在能取的帧都取出来：转格式、写声卡、写文件
 * @param[in]     decoder           解码器
 * @param[in]     resampler         格式转换
 * @param[in]     output            声卡
 * @param[in]     pcmFile           PCM 文件；nullptr = 不写
 * @param[in,out] playedFrameCount  一共播了多少帧（采样），累加
 * @return true = 正常；false = 出错
 * @note decodedFramePtr、outputSamples 每次调用都新建，M3 只是验证，不在乎这点开销
 */
static bool playDecodedFrames(SoftwareAudioDecoder &decoder, AudioResampler &resampler, AlsaAudioOutput &output,
                              FILE *pcmFile, int64_t &playedFrameCount) {
    AvFramePtr decodedFramePtr;
    std::vector<int16_t> outputSamples;
    while (true) {
        const ReceiveFrameResult receiveResult = decoder.receiveFrame(decodedFramePtr);
        if (receiveResult == ReceiveFrameResult::NeedMoreInput || receiveResult == ReceiveFrameResult::EndOfStream) {
            return true;
        }
        if (receiveResult == ReceiveFrameResult::Error) {
            return false;
        }
        const int outputFrameCount = resampler.convert(decodedFramePtr.get(), outputSamples);
        if (outputFrameCount <= 0) {
            continue;
        }
        if (pcmFile) {
            fwrite(outputSamples.data(), sizeof(int16_t), outputSamples.size(), pcmFile);
        }
        if (!output.write(outputSamples.data(), outputFrameCount)) {
            return false;
        }
        playedFrameCount += outputFrameCount;
    }
}

/**
 * @brief M3 主流程
 * @param[in] inputPath      mp4 路径
 * @param[in] pcmOutputPath  PCM 文件路径，可以是 nullptr
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 打开 mp4、解码器、声卡；第 2 步 循环读包，只要音频包，解码播放；第 3 步 送"没有了"，播完剩下的；
 *       第 4 步 等声卡播空，打印统计
 */
int runM3PlayAudioOnly(const char *inputPath, const char *pcmOutputPath) {
    // 第 1 步 打开
    Mp4Demuxer demuxer;
    if (!demuxer.open(inputPath)) {
        return 1;
    }
    const AVStream *audioStream = demuxer.getAudioStream();
    if (!audioStream) {
        LOGE(kTag, "%s 里没有音频流", inputPath);
        return 1;
    }
    SoftwareAudioDecoder decoder;
    AudioResampler resampler(kOutputSampleRate, kOutputChannelCount);
    AlsaAudioOutput output;
    if (!decoder.init(audioStream) ||
        !output.open(getAlsaDeviceName(), kOutputSampleRate, kOutputChannelCount, kAlsaBufferDurationUs)) {
        return 1;
    }
    FilePtr pcmFilePtr;
    if (pcmOutputPath) {
        pcmFilePtr.reset(fopen(pcmOutputPath, "wb"));
        if (!pcmFilePtr) {
            LOGE(kTag, "打不开输出文件 %s", pcmOutputPath);
            return 1;
        }
    }

    // 第 2 步 循环读包
    const int64_t startTimeUs = getSteadyTimeUs();
    int audioPacketCount = 0;
    int64_t playedFrameCount = 0;
    while (true) {
        AvPacketPtr packetPtr;
        if (demuxer.readPacket(packetPtr) != ReadPacketResult::Packet) {
            break;
        }
        if (packetPtr->stream_index != audioStream->index) {
            continue; // 视频包不要
        }
        audioPacketCount++;
        if (!decoder.sendPacket(packetPtr.get()) ||
            !playDecodedFrames(decoder, resampler, output, pcmFilePtr.get(), playedFrameCount)) {
            return 1;
        }
    }

    // 第 3 步 送 nullptr：解码器把缓存的数据吐出来
    if (decoder.sendPacket(nullptr)) {
        playDecodedFrames(decoder, resampler, output, pcmFilePtr.get(), playedFrameCount);
    }

    // 第 4 步 等声卡把最后约 100 ms 播完，不然程序退出时 snd_pcm_close 会把它丢掉
    while (output.getQueuedDurationUs() > 0) {
        sleepForUs(10 * 1000);
    }
    const double elapsedSeconds = (getSteadyTimeUs() - startTimeUs) / 1e6;
    const double playedSeconds = static_cast<double>(playedFrameCount) / kOutputSampleRate;
    LOGI(kTag, "音频包 %d 个，播了 %.3f 秒的声音（%lld 帧），实际用时 %.3f 秒%s%s", audioPacketCount, playedSeconds,
         static_cast<long long>(playedFrameCount), elapsedSeconds, pcmOutputPath ? " → " : "",
         pcmOutputPath ? pcmOutputPath : "");
    return 0;
}
