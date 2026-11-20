#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/encoded_frame.h"
#include "common/ffmpeg_utils.h"

struct VideoTrackParams {
    VideoCodec codec = VideoCodec::H264;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> extradataAnnexb; // vps/sps/pps 各自前面补 00 00 00 01 拼起来
};

VideoTrackParams buildVideoTrackParams(const StreamInfo &streamInfo, int width, int height);

// 一个 MP4 文件。只封装、不解码：码流原样进 mdat（Annex-B 转长度前缀由 libavformat 的 mp4 封装器做）
class Mp4FileWriter {
public:
    Mp4FileWriter() = default;
    Mp4FileWriter(const Mp4FileWriter &) = delete;
    Mp4FileWriter &operator=(const Mp4FileWriter &) = delete;
    ~Mp4FileWriter() { close(); }

    // shouldFragment：true = fMP4（M4 用），false = 普通 MP4
    bool open(const std::string &requestedFilePath, const VideoTrackParams &trackParams, bool shouldFragment);
    bool writeFrame(const EncodedFrame &frame, int64_t ptsIn90kHz); // ptsIn90kHz：这个文件里从 0 开始
    bool close();                                                    // 写 trailer（moov）、关文件
    bool isOpen() const { return outputContextPtr != nullptr; }
    const std::string &getFilePath() const { return filePath; }
    int64_t getWrittenBytes() const { return writtenBytes; }
    int getWrittenFrameCount() const { return writtenFrameCount; }

private:
    AvOutputFormatContextPtr outputContextPtr;
    AVStream *videoStream = nullptr; // 归 outputContextPtr 所有
    AvPacketPtr packetPtr;
    std::string filePath;
    int64_t lastPtsIn90kHz = -1;
    int64_t writtenBytes = 0;
    int writtenFrameCount = 0;
};
