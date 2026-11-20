// Mac 上跑：./build-mac/n3_mac_tests [aaa.264 路径]
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "common/annexb_utils.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/mp4_file_writer.h"
#include "storage/record_index.h"
#include "storage/record_path_builder.h"
#include "storage/segment_recorder.h"

#include <algorithm>
#include <vector>

static int g_failedCount = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (condition) {                                                                                               \
            std::printf("  通过：%s\n", #condition);                                                                 \
        } else {                                                                                                       \
            std::printf("  失败：%s（%s:%d）\n", #condition, __FILE__, __LINE__);                                    \
            g_failedCount++;                                                                                           \
        }                                                                                                              \
    } while (0)

static void testAnnexBSplit() {
    std::printf("[AnnexB 切分]\n");
    // 4 字节起始码 SPS(67) + 3 字节起始码 PPS(68) + 4 字节起始码 IDR(65)，IDR 末尾带一个 trailing zero
    const uint8_t data[] = {0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1F, 0, 0, 1, 0x68, 0xCE, 0x3C, 0x80,
                            0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00};
    const auto nalUnits = splitAnnexBNalUnits(data, sizeof(data));
    CHECK(nalUnits.size() == 3);
    CHECK(nalUnits.size() == 3 && nalUnits[0].sizeBytes == 4);
    CHECK(nalUnits.size() == 3 && nalUnits[1].sizeBytes == 4);
    CHECK(nalUnits.size() == 3 && nalUnits[2].sizeBytes == 3);
    CHECK(containsKeyFrameNal(VideoCodec::H264, data, sizeof(data)));
    const ParameterSets parameterSets = extractParameterSets(VideoCodec::H264, data, sizeof(data));
    CHECK(parameterSets.sps.size() == 4 && parameterSets.sps[0] == 0x67);
    CHECK(parameterSets.pps.size() == 4 && parameterSets.pps[0] == 0x68);
    CHECK(parameterSets.vps.empty());
    const std::vector<uint8_t> extradata = buildAnnexBExtradata(parameterSets);
    CHECK(extradata.size() == 16 && extradata[3] == 1 && extradata[4] == 0x67);

    const uint8_t pFrame[] = {0, 0, 0, 1, 0x41, 0x9A, 0x02};
    CHECK(!containsKeyFrameNal(VideoCodec::H264, pFrame, sizeof(pFrame)));
    // H.265：类型在第 1 字节的第 1～6 位。0x26 = 19 << 1 → IDR_W_RADL；0x02 = 1 << 1 → TRAIL_R
    const uint8_t h265Idr[] = {0, 0, 1, 0x26, 0x01, 0xAF};
    const uint8_t h265Trail[] = {0, 0, 1, 0x02, 0x01, 0xD0};
    CHECK(containsKeyFrameNal(VideoCodec::H265, h265Idr, sizeof(h265Idr)));
    CHECK(!containsKeyFrameNal(VideoCodec::H265, h265Trail, sizeof(h265Trail)));
}

// 读 aaa.264 全部帧：帧数、关键帧数、时间戳
static void testRawFileSource(const std::string &inputPath) {
    std::printf("[读裸流 %s]\n", inputPath.c_str());
    FfmpegFrameSource frameSource;
    CHECK(frameSource.open(inputPath, 30));
    CHECK(frameSource.isRawStreamFile());
    CHECK(!frameSource.getStreamInfo().sps.empty());
    CHECK(!frameSource.getStreamInfo().pps.empty());
    CHECK(frameSource.getStreamInfo().width == 1920 && frameSource.getStreamInfo().height == 1080);
    int frameCount = 0;
    int keyFrameCount = 0;
    int64_t lastTimestamp = -1;
    bool isFirstFrameKey = false;
    EncodedFrame frame;
    while (frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady) {
        if (frameCount == 0) {
            isFirstFrameKey = frame.isKeyFrame;
        }
        keyFrameCount += frame.isKeyFrame ? 1 : 0;
        lastTimestamp = frame.extendedRtpTimestamp;
        frameCount++;
    }
    std::printf("  帧数 %d，关键帧 %d，最后时间戳 %lld\n", frameCount, keyFrameCount, static_cast<long long>(lastTimestamp));
    CHECK(frameCount == 1803);
    CHECK(keyFrameCount == 36);
    CHECK(isFirstFrameKey);
    CHECK(lastTimestamp == 1802LL * 3000);
}

// 写一个短 MP4（前 90 帧），再用 libavformat 读回来：帧数、编码格式
static void testMp4RoundTrip(const std::string &inputPath, const std::string &outputPath) {
    std::printf("[写 MP4 再读回 %s]\n", outputPath.c_str());
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputPath, 30)) {
        CHECK(false);
        return;
    }
    Mp4FileWriter mp4Writer;
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    CHECK(mp4Writer.open(outputPath, buildVideoTrackParams(streamInfo, streamInfo.width, streamInfo.height), false));
    EncodedFrame frame;
    while (mp4Writer.getWrittenFrameCount() < 90 && frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady) {
        mp4Writer.writeFrame(frame, frame.extendedRtpTimestamp);
    }
    // 重复的 pts 要被丢掉，不能让封装器报错
    CHECK(mp4Writer.writeFrame(frame, frame.extendedRtpTimestamp));
    CHECK(mp4Writer.getWrittenFrameCount() == 90);
    CHECK(mp4Writer.close());

    AVFormatContext *rawInputContext = nullptr;
    CHECK(avformat_open_input(&rawInputContext, outputPath.c_str(), nullptr, nullptr) == 0);
    AvInputFormatContextPtr inputContextPtr(rawInputContext);
    CHECK(inputContextPtr && avformat_find_stream_info(inputContextPtr.get(), nullptr) >= 0);
    if (!inputContextPtr) {
        return;
    }
    const AVStream *stream = inputContextPtr->streams[0];
    CHECK(stream->codecpar->codec_id == AV_CODEC_ID_H264);
    CHECK(stream->nb_frames == 90);
    // avcC：extradata 第一个字节是 1（configurationVersion），说明封装器把 Annex-B 转成了 avcC
    CHECK(stream->codecpar->extradata_size > 0 && stream->codecpar->extradata[0] == 1);
    const double durationSeconds = stream->duration * av_q2d(stream->time_base);
    std::printf("  time_base 1/%d，时长 %.3f 秒\n", stream->time_base.den, durationSeconds);
    CHECK(durationSeconds > 2.9 && durationSeconds < 3.1);
}

// 读回一个 MP4：帧数、时长（秒）、第一帧是不是关键帧
struct Mp4Summary {
    int frameCount = 0;
    double durationSeconds = 0;
    bool isFirstFrameKey = false;
};

static Mp4Summary summarizeMp4(const std::string &filePath) {
    Mp4Summary summary;
    AVFormatContext *rawInputContext = nullptr;
    if (avformat_open_input(&rawInputContext, filePath.c_str(), nullptr, nullptr) != 0) {
        return summary;
    }
    AvInputFormatContextPtr inputContextPtr(rawInputContext);
    avformat_find_stream_info(inputContextPtr.get(), nullptr);
    const AVStream *stream = inputContextPtr->streams[0];
    summary.durationSeconds = stream->duration * av_q2d(stream->time_base);
    AvPacketPtr packetPtr = allocateAvPacket();
    while (av_read_frame(inputContextPtr.get(), packetPtr.get()) == 0) {
        if (summary.frameCount == 0) {
            summary.isFirstFrameKey = (packetPtr->flags & AV_PKT_FLAG_KEY) != 0;
        }
        summary.frameCount++;
        av_packet_unref(packetPtr.get());
    }
    return summary;
}

static std::vector<std::string> listMp4Files(const std::string &rootPath) {
    std::vector<std::string> filePaths;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(rootPath)) {
        if (entry.path().extension() == ".mp4") {
            filePaths.push_back(entry.path().string());
        }
    }
    std::sort(filePaths.begin(), filePaths.end());
    return filePaths;
}

static void testRecordPath(const std::string &rootPath) {
    std::printf("[录像路径]\n");
    // 2027-02-03 14:05:00 本地时间
    tm localTime{};
    localTime.tm_year = 2027 - 1900;
    localTime.tm_mon = 1;
    localTime.tm_mday = 3;
    localTime.tm_hour = 14;
    localTime.tm_min = 5;
    localTime.tm_isdst = -1;
    const int64_t startTimeMs = static_cast<int64_t>(mktime(&localTime)) * 1000;
    const std::string firstPath = buildSegmentFilePath(rootPath, 2, startTimeMs);
    CHECK(firstPath == rootPath + "/ch2/2027-02-03/14-05-00.mp4");
    CHECK(std::filesystem::is_directory(rootPath + "/ch2/2027-02-03"));
    std::FILE *file = std::fopen(firstPath.c_str(), "wb");
    if (file != nullptr) {
        std::fclose(file);
    }
    CHECK(buildSegmentFilePath(rootPath, 2, startTimeMs) == rootPath + "/ch2/2027-02-03/14-05-00_1.mp4");
}

// 循环读 aaa.264，录 30 秒、每段 5 秒：每段第一帧是关键帧、时长 5～7 秒、帧数加起来 = 写入帧数
static void testSegmentRecorder(const std::string &inputPath, const std::string &rootPath) {
    std::printf("[分段录像 30 秒 / 每段 5 秒]\n");
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputPath, 30)) {
        CHECK(false);
        return;
    }
    frameSource.setShouldLoopRawFile(true);
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    SegmentRecorder segmentRecorder;
    SegmentRecorderConfig recorderConfig;
    recorderConfig.recordRootPath = rootPath;
    recorderConfig.segmentDurationSeconds = 5;
    CHECK(segmentRecorder.open(recorderConfig, buildVideoTrackParams(streamInfo, streamInfo.width, streamInfo.height)));
    EncodedFrame frame;
    int receivedFrameCount = 0;
    while (receivedFrameCount < 900 && frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady) {
        receivedFrameCount++;
        if (!segmentRecorder.writeFrame(frame)) {
            CHECK(false);
            break;
        }
    }
    CHECK(segmentRecorder.close());
    const std::vector<std::string> filePaths = listMp4Files(rootPath);
    std::printf("  收到 %d 帧，写入 %d 帧，%d 个文件\n", receivedFrameCount, segmentRecorder.getWrittenFrameCount(),
                static_cast<int>(filePaths.size()));
    CHECK(segmentRecorder.getClosedSegmentCount() == static_cast<int>(filePaths.size()));
    CHECK(filePaths.size() >= 5 && filePaths.size() <= 6);
    int totalFrameCount = 0;
    bool isEveryFirstFrameKey = true;
    bool isEveryDurationOk = true;
    for (size_t fileIndex = 0; fileIndex < filePaths.size(); ++fileIndex) {
        const Mp4Summary summary = summarizeMp4(filePaths[fileIndex]);
        std::printf("  %s：%d 帧，%.3f 秒，首帧关键帧 %d\n", filePaths[fileIndex].c_str() + rootPath.size(),
                    summary.frameCount, summary.durationSeconds, summary.isFirstFrameKey);
        totalFrameCount += summary.frameCount;
        isEveryFirstFrameKey = isEveryFirstFrameKey && summary.isFirstFrameKey;
        const bool isLastFile = fileIndex + 1 == filePaths.size();
        if (!isLastFile && (summary.durationSeconds < 5.0 || summary.durationSeconds > 7.05)) {
            isEveryDurationOk = false;
        }
    }
    CHECK(isEveryFirstFrameKey);
    CHECK(isEveryDurationOk);
    CHECK(totalFrameCount == segmentRecorder.getWrittenFrameCount());
    CHECK(segmentRecorder.getWrittenFrameCount() + segmentRecorder.getSkippedBeforeKeyFrameCount() == receivedFrameCount);
}

// 时间戳倒退：倒退后的非关键帧丢掉，下一个关键帧开新段
static void testDiscontinuity(const std::string &inputPath, const std::string &rootPath) {
    std::printf("[时间戳倒退]\n");
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputPath, 30)) {
        CHECK(false);
        return;
    }
    std::vector<EncodedFrame> frames;
    EncodedFrame frame;
    while (frames.size() < 300 && frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady) {
        frames.push_back(frame);
    }
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    SegmentRecorder segmentRecorder;
    SegmentRecorderConfig recorderConfig;
    recorderConfig.recordRootPath = rootPath;
    recorderConfig.segmentDurationSeconds = 600;
    CHECK(segmentRecorder.open(recorderConfig, buildVideoTrackParams(streamInfo, streamInfo.width, streamInfo.height)));
    for (const EncodedFrame &firstPassFrame : frames) {
        segmentRecorder.writeFrame(firstPassFrame);
    }
    // 第二遍：时间戳从 0 重来（像上游重启）。从第 1 帧（非关键帧）开始送，应该丢到下一个关键帧
    int expectedSkippedCount = 0;
    bool hasSeenKeyFrame = false;
    for (size_t frameIndex = 1; frameIndex < frames.size(); ++frameIndex) {
        hasSeenKeyFrame = hasSeenKeyFrame || frames[frameIndex].isKeyFrame;
        expectedSkippedCount += hasSeenKeyFrame ? 0 : 1;
        segmentRecorder.writeFrame(frames[frameIndex]);
    }
    CHECK(segmentRecorder.close());
    std::printf("  丢掉 %d 帧（预期 %d），%d 个文件\n", segmentRecorder.getSkippedBeforeKeyFrameCount(),
                expectedSkippedCount, segmentRecorder.getClosedSegmentCount());
    CHECK(segmentRecorder.getClosedSegmentCount() == 2);
    CHECK(segmentRecorder.getSkippedBeforeKeyFrameCount() == expectedSkippedCount);
    CHECK(expectedSkippedCount > 0);
}

// RecordIndex：插入、补结束时间、按时间段查重叠、查询计划走索引
static void testRecordIndex(const std::string &databaseFilePath) {
    std::printf("[录像索引]\n");
    RecordIndex recordIndex;
    CHECK(recordIndex.open(databaseFilePath));
    // ch0 三段 [0,60000) [60000,120000) [120000, 还在录)；ch1 一段 [0,60000)
    const int64_t firstId = recordIndex.insertRecordingStart(0, 0, "/rec/ch0/a.mp4");
    const int64_t secondId = recordIndex.insertRecordingStart(0, 60000, "/rec/ch0/b.mp4");
    const int64_t thirdId = recordIndex.insertRecordingStart(0, 120000, "/rec/ch0/c.mp4");
    const int64_t otherChannelId = recordIndex.insertRecordingStart(1, 0, "/rec/ch1/a.mp4");
    CHECK(firstId > 0 && secondId > firstId && thirdId > secondId && otherChannelId > thirdId);
    CHECK(recordIndex.insertRecordingStart(0, 0, "/rec/ch0/a.mp4") == -1); // file_path UNIQUE
    CHECK(recordIndex.finishRecording(firstId, 60000, 1000));
    CHECK(recordIndex.finishRecording(secondId, 120000, 2000));
    CHECK(recordIndex.finishRecording(otherChannelId, 60000, 3000));
    CHECK(!recordIndex.finishRecording(9999, 1, 1));

    std::vector<RecordingEntry> entries = recordIndex.queryOverlapping(0, 30000, 90000);
    CHECK(entries.size() == 2 && entries[0].recordingId == firstId && entries[1].recordingId == secondId);
    CHECK(entries.size() == 2 && entries[0].sizeBytes == 1000 && entries[0].filePath == "/rec/ch0/a.mp4");
    entries = recordIndex.queryOverlapping(0, 60000, 120000); // 端点相接不算重叠
    CHECK(entries.size() == 1 && entries[0].recordingId == secondId);
    entries = recordIndex.queryOverlapping(0, 500000, 600000); // 还在录的那段（end 为 NULL）
    CHECK(entries.size() == 1 && entries[0].recordingId == thirdId && entries[0].endTimeMs == kUnfinishedEndTimeMs);
    entries = recordIndex.queryOverlapping(1, 0, 1000000);
    CHECK(entries.size() == 1 && entries[0].recordingId == otherChannelId);

    bool isUsingIndex = false;
    for (const std::string &planLine : recordIndex.explainOverlappingQueryPlan()) {
        std::printf("  查询计划：%s\n", planLine.c_str());
        isUsingIndex = isUsingIndex || planLine.find("idx_recordings_channel_start") != std::string::npos;
    }
    CHECK(isUsingIndex);

    // 关掉再打开：数据还在，建表语句不报错
    recordIndex.close();
    CHECK(recordIndex.open(databaseFilePath));
    CHECK(recordIndex.queryOverlapping(0, 0, 1000000).size() == 3);
}

// SegmentRecorder + RecordIndex：每段一条，大小 = 文件大小，相邻两段首尾相接（差 < 1 帧 33 ms）
static void testSegmentRecorderWithIndex(const std::string &inputPath, const std::string &rootPath) {
    std::printf("[分段录像 + 索引 30 秒 / 每段 5 秒]\n");
    std::filesystem::create_directories(rootPath);
    RecordIndex recordIndex;
    CHECK(recordIndex.open(rootPath + "/record_index.db"));
    FfmpegFrameSource frameSource;
    if (!frameSource.open(inputPath, 30)) {
        CHECK(false);
        return;
    }
    const StreamInfo &streamInfo = frameSource.getStreamInfo();
    SegmentRecorder segmentRecorder;
    SegmentRecorderConfig recorderConfig;
    recorderConfig.recordRootPath = rootPath;
    recorderConfig.segmentDurationSeconds = 5;
    CHECK(segmentRecorder.open(recorderConfig, buildVideoTrackParams(streamInfo, streamInfo.width, streamInfo.height),
                               &recordIndex));
    EncodedFrame frame;
    for (int frameIndex = 0; frameIndex < 900 && frameSource.receiveNextFrame(frame) == ReceiveResult::FrameReady;
         ++frameIndex) {
        segmentRecorder.writeFrame(frame);
    }
    CHECK(segmentRecorder.close());
    const std::vector<RecordingEntry> entries = recordIndex.queryOverlapping(0, 0, INT64_MAX);
    CHECK(static_cast<int>(entries.size()) == segmentRecorder.getClosedSegmentCount());
    bool isEverySizeOk = true;
    bool isEveryGapOk = true;
    for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
        const RecordingEntry &entry = entries[entryIndex];
        const int64_t fileSizeBytes = static_cast<int64_t>(std::filesystem::file_size(entry.filePath));
        isEverySizeOk = isEverySizeOk && entry.endTimeMs != kUnfinishedEndTimeMs && entry.sizeBytes == fileSizeBytes;
        const Mp4Summary summary = summarizeMp4(entry.filePath);
        const double indexDurationSeconds = (entry.endTimeMs - entry.startTimeMs) / 1000.0;
        std::printf("  id %lld：%.3f 秒（索引）/ %.3f 秒（文件），%lld 字节\n", static_cast<long long>(entry.recordingId),
                    indexDurationSeconds, summary.durationSeconds, static_cast<long long>(entry.sizeBytes));
        if (entryIndex + 1 < entries.size()) {
            const int64_t gapMs = entries[entryIndex + 1].startTimeMs - entry.endTimeMs;
            isEveryGapOk = isEveryGapOk && gapMs >= -33 && gapMs <= 33;
        }
    }
    CHECK(entries.size() == 6);
    CHECK(isEverySizeOk);
    CHECK(isEveryGapOk);
}

int main(int argc, char **argv) {
    const std::string inputPath = argc > 1 ? argv[1] : "/Users/dev/Documents/AV/rk_test_data/aaa.264";
    const std::string outputPath = (std::filesystem::temp_directory_path() / "n3_mac_test_90.mp4").string();
    testAnnexBSplit();
    testRawFileSource(inputPath);
    testMp4RoundTrip(inputPath, outputPath);
    const std::filesystem::path tempRootPath = std::filesystem::temp_directory_path() / "n3_mac_test_rec";
    std::filesystem::remove_all(tempRootPath);
    testRecordPath((tempRootPath / "path").string());
    testSegmentRecorder(inputPath, (tempRootPath / "segment").string());
    testDiscontinuity(inputPath, (tempRootPath / "discontinuity").string());
    std::filesystem::create_directories(tempRootPath);
    testRecordIndex((tempRootPath / "index_test.db").string());
    testSegmentRecorderWithIndex(inputPath, (tempRootPath / "segment_index").string());
    std::printf(g_failedCount == 0 ? "全部通过\n" : "有 %d 条失败\n", g_failedCount);
    return g_failedCount == 0 ? 0 : 1;
}
