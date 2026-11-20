// Mac 上跑：./build-mac/n3_mac_tests [aaa.264 路径]
#include <cstdio>
#include <filesystem>
#include <string>

#include "common/annexb_utils.h"
#include "source/ffmpeg_frame_source.h"
#include "storage/mp4_file_writer.h"

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

int main(int argc, char **argv) {
    const std::string inputPath = argc > 1 ? argv[1] : "/Users/dev/Documents/AV/rk_test_data/aaa.264";
    const std::string outputPath = (std::filesystem::temp_directory_path() / "n3_mac_test_90.mp4").string();
    testAnnexBSplit();
    testRawFileSource(inputPath);
    testMp4RoundTrip(inputPath, outputPath);
    std::printf(g_failedCount == 0 ? "全部通过\n" : "有 %d 条失败\n", g_failedCount);
    return g_failedCount == 0 ? 0 : 1;
}
