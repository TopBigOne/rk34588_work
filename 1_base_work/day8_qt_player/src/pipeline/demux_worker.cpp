#include "pipeline/demux_worker.h"

#include <utility> // std::move

#include "common/log.h"
#include "common/time_utils.h"

static const char *const kTag = "DemuxWorker";

/**
 * @brief 保存引用，算出两个流的序号
 * @param[in] requestedDemuxer     demuxer
 * @param[in] requestedVideoQueue  视频包队列
 * @param[in] requestedAudioQueue  音频包队列
 * @param[in] requestedClock       时钟
 * @param[in] requestedState       状态
 * @note 队列是 nullptr 的那种流，读到了也直接扔掉
 */
DemuxWorker::DemuxWorker(Mp4Demuxer &requestedDemuxer, PacketQueue *requestedVideoQueue,
                         PacketQueue *requestedAudioQueue, MediaClock &requestedClock, PlaybackState &requestedState) :
    demuxer(requestedDemuxer), videoQueue(requestedVideoQueue), audioQueue(requestedAudioQueue),
    clock(requestedClock), state(requestedState) {
    const AVStream *videoStream = demuxer.getVideoStream();
    const AVStream *audioStream = demuxer.getAudioStream();
    videoStreamIndex = (videoQueue && videoStream) ? videoStream->index : -1;
    audioStreamIndex = (audioQueue && audioStream) ? audioStream->index : -1;
}

/**
 * @brief 线程函数
 * @return 无
 * @note 每一轮：第 1 步 有 seek 请求就先处理；第 2 步 已经读到末尾就睡一会儿再看；
 *       第 3 步 读一个包；第 4 步 按流分到队列（满了就等）
 */
void DemuxWorker::run() {
    LOGI(kTag, "开始");
    bool isEndOfFile = false;
    int readPacketCount = 0;
    while (!state.shouldStop) {
        // 第 1 步 exchange(false)：读出旧值并同时改成 false，一步完成，不会漏掉也不会处理两次
        if (state.isSeekRequested.exchange(false)) {
            performSeek(state.requestedSeekTargetUs);
            isEndOfFile = false;
            continue;
        }

        // 第 2 步 读到末尾以后没事做，每 10 ms 看一眼有没有 seek 或停止
        if (isEndOfFile) {
            sleepForUs(10 * 1000);
            continue;
        }

        // 第 3 步 读一个包
        AvPacketPtr packetPtr;
        const ReadPacketResult readResult = demuxer.readPacket(packetPtr);
        if (readResult != ReadPacketResult::Packet) {
            // 读完了，或者读出错（文件坏了）：都当成结束，让后面把已经收到的播完
            LOGI(kTag, "%s，共读了 %d 个包", readResult == ReadPacketResult::EndOfFile ? "文件读完了" : "读包出错",
                 readPacketCount);
            if (!pushEndOfStreamMarkers()) {
                break;
            }
            isEndOfFile = true;
            continue;
        }
        readPacketCount++;

        // 第 4 步 按 stream_index 分发；不要的流（字幕、第二条音轨……）直接扔掉，packetPtr 析构时释放
        const int streamIndex = packetPtr->stream_index;
        PacketQueue *targetQueue = nullptr;
        if (streamIndex == videoStreamIndex) {
            targetQueue = videoQueue;
        } else if (streamIndex == audioStreamIndex) {
            targetQueue = audioQueue;
        }
        if (targetQueue) {
            QueuedPacket queuedPacket;
            queuedPacket.packetPtr = std::move(packetPtr);
            queuedPacket.serial = state.serial;
            if (!targetQueue->push(std::move(queuedPacket))) {
                break; // 队列被叫停：播放器要关了
            }
        }
    }
    LOGI(kTag, "退出");
}

/**
 * @brief 处理一次 seek
 * @param[in] targetUs  目标时间
 * @return 无
 * @note 第 1 步 跳到目标前最近的关键帧；第 2 步 先写目标时间、再把 serial 加 1；第 3 步 重设时钟；第 4 步 清空队列
 */
void DemuxWorker::performSeek(int64_t targetUs) {
    // 第 1 步 跳转。失败（极少见）也接着往下走：serial 照样加 1，后面从当前位置接着读
    demuxer.seekTo(targetUs);

    // 第 2 步 先写目标时间，再写 serial：别的线程一看到新 serial，读到的目标时间就已经是新的
    const int newSerial = state.serial + 1;
    state.seekTargetUs = targetUs;
    state.serial = newSerial;

    // 第 3 步 时钟停在目标时间，等新位置的第一个数据来了再走
    clock.reset(targetUs, newSerial);

    // 第 4 步 清空队列里的旧包。视频、音频线程手里可能还拿着一个旧包，它们会看 serial 自己扔掉
    if (videoQueue) {
        videoQueue->clear();
    }
    if (audioQueue) {
        audioQueue->clear();
    }
    LOGI(kTag, "seek 到 %.3f 秒，serial = %d", targetUs / 1e6, newSerial);
}

/**
 * @brief 放"结束"标记
 * @return true = 成功；false = 被叫停
 * @note 结束标记没有包，只有 serial 和 isEndOfStream
 */
bool DemuxWorker::pushEndOfStreamMarkers() {
    PacketQueue *queues[] = {videoQueue, audioQueue};
    for (PacketQueue *queue : queues) {
        if (!queue) {
            continue;
        }
        QueuedPacket endMarker;
        endMarker.serial = state.serial;
        endMarker.isEndOfStream = true;
        if (!queue->push(std::move(endMarker))) {
            return false;
        }
    }
    return true;
}
