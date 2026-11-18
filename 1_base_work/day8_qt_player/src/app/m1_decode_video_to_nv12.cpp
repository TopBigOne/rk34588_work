#include "app/m1_decode_video_to_nv12.h"

#include <cstdio> // fopen、fwrite
#include <utility> // std::move
#include <vector>

#include "common/ffmpeg_utils.h"
#include "common/log.h"
#include "common/mpp_utils.h"
#include "common/time_utils.h"
#include "demux/mp4_demuxer.h"
#include "video/annexb_converter.h"
#include "video/mpp_video_decoder.h"

static const char *const kTag = "M1";

/**
 * @brief 把一帧 NV12 去掉 stride 填充后写进文件
 * @param[in] frame       解码帧
 * @param[in] outputFile  输出文件
 * @return true = 写成功；false = 失败
 * @note 解码器的 buffer 每行 horStride 字节、一共 verStride 行（H.264 1080p 是 1920×1088），
 *       有效画面只有 width × height。Y 平面在前（verStride 行），UV 平面紧跟其后（verStride / 2 行）。
 *       逐行只写有效的 width 字节：先 height 行 Y，再 height / 2 行 UV
 */
static bool writeCompactNv12(MppFrame frame, FILE *outputFile) {
    MppBuffer frameBuffer = mpp_frame_get_buffer(frame);
    // mpp_buffer_get_ptr：这块 buffer 映射到 CPU 的地址（DRM buffer 由 MPP 内部 mmap 好了）
    const uint8_t *bufferStart = static_cast<const uint8_t *>(mpp_buffer_get_ptr(frameBuffer));
    const size_t width = mpp_frame_get_width(frame);
    const size_t height = mpp_frame_get_height(frame);
    const size_t horStride = mpp_frame_get_hor_stride(frame);
    const size_t verStride = mpp_frame_get_ver_stride(frame);

    const uint8_t *yPlane = bufferStart;                         // Y 平面从 buffer 开头开始
    const uint8_t *uvPlane = bufferStart + horStride * verStride; // UV 平面在 Y 平面（含填充行）后面
    for (size_t rowIndex = 0; rowIndex < height; rowIndex++) {
        if (fwrite(yPlane + rowIndex * horStride, 1, width, outputFile) != width) {
            return false;
        }
    }
    for (size_t rowIndex = 0; rowIndex < height / 2; rowIndex++) {
        if (fwrite(uvPlane + rowIndex * horStride, 1, width, outputFile) != width) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 把解码器里现在能取的帧都取出来写文件
 * @param[in]     decoder            解码器
 * @param[in]     outputFile         输出文件
 * @param[in]     maxFrameCount      最多写几帧，0 = 不限
 * @param[in,out] writtenFrameCount  已经写了几帧，每写一帧加 1
 * @param[out]    hasReachedEos      取到带 EOS 标志的帧时设成 true
 * @return true = 正常（包括写够了帧数提前返回）；false = 出错
 * @note 带错误标志的帧也照写（保持帧数对得上），但打一条警告。
 *       写够 maxFrameCount 帧就停：解码器里可能一次攒着好几帧，不在这里停的话会多写
 *       （2026-11-18 上板实测：要 300 帧写出了 306 帧）
 */
static bool drainFramesToFile(MppVideoDecoder &decoder, FILE *outputFile, int maxFrameCount, int &writtenFrameCount,
                              bool &hasReachedEos) {
    while (maxFrameCount <= 0 || writtenFrameCount < maxFrameCount) {
        MppFramePtr framePtr;
        const GetFrameResult getResult = decoder.getFrame(framePtr);
        if (getResult == GetFrameResult::None) {
            return true;
        }
        if (getResult == GetFrameResult::Error) {
            return false;
        }
        MppFrame frame = framePtr.get();
        if (mpp_frame_get_eos(frame)) {
            hasReachedEos = true;
        }
        if (!mpp_frame_get_buffer(frame)) {
            continue; // 只带 EOS 标志的空帧
        }
        if (mpp_frame_get_errinfo(frame) || mpp_frame_get_discard(frame)) {
            LOGW(kTag, "第 %d 帧带错误标志", writtenFrameCount);
        }
        if (!writeCompactNv12(frame, outputFile)) {
            LOGE(kTag, "写文件失败");
            return false;
        }
        writtenFrameCount++;
    }
    return true; // 写够了
}

/**
 * @brief M1 主流程
 * @param[in] inputPath      mp4 路径
 * @param[in] outputPath     nv12 路径
 * @param[in] maxFrameCount  最多几帧
 * @return 0 = 成功；1 = 失败
 * @note 第 1 步 打开 mp4、建 Annex B 转换和解码器；第 2 步 循环读包，只要视频包，转换后送进解码器、取帧写文件；
 *       第 3 步 文件读完送 EOS，把剩下的帧取完；第 4 步 打印统计
 */
int runM1DecodeVideoToNv12(const char *inputPath, const char *outputPath, int maxFrameCount) {
    // 第 1 步 打开
    Mp4Demuxer demuxer;
    if (!demuxer.open(inputPath)) {
        return 1;
    }
    const AVStream *videoStream = demuxer.getVideoStream();
    if (!videoStream) {
        LOGE(kTag, "%s 里没有视频流", inputPath);
        return 1;
    }
    AnnexbConverter annexbConverter;
    MppVideoDecoder decoder;
    const MppCodingType codingType =
            videoStream->codecpar->codec_id == AV_CODEC_ID_HEVC ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    if (!annexbConverter.init(videoStream) || !decoder.init(codingType)) {
        return 1;
    }
    FilePtr outputFilePtr(fopen(outputPath, "wb")); // "wb"：写、二进制
    if (!outputFilePtr) {
        LOGE(kTag, "打不开输出文件 %s", outputPath);
        return 1;
    }

    // 第 2 步 循环读包
    const int64_t startTimeUs = getSteadyTimeUs();
    int videoPacketCount = 0;
    int writtenFrameCount = 0;
    bool hasReachedEos = false;
    while (maxFrameCount <= 0 || writtenFrameCount < maxFrameCount) {
        AvPacketPtr packetPtr;
        if (demuxer.readPacket(packetPtr) != ReadPacketResult::Packet) {
            break; // 读完了（或出错）
        }
        if (packetPtr->stream_index != videoStream->index) {
            continue; // 音频包不要
        }
        videoPacketCount++;
        const int64_t packetTimestamp = packetPtr->pts != AV_NOPTS_VALUE ? packetPtr->pts : packetPtr->dts;
        const int64_t ptsUs = convertToMicroseconds(packetTimestamp, videoStream->time_base);

        std::vector<AvPacketPtr> annexbPackets;
        if (!annexbConverter.convert(std::move(packetPtr), annexbPackets)) {
            return 1;
        }
        for (const AvPacketPtr &annexbPacketPtr : annexbPackets) {
            // 第一个包打印开头几个字节：应该是起始码 00 00 00 01，证明 Annex B 转换生效了
            if (videoPacketCount == 1) {
                const uint8_t *data = annexbPacketPtr->data;
                LOGI(kTag, "第一个包 %d 字节，开头 %02x %02x %02x %02x %02x", annexbPacketPtr->size, data[0], data[1],
                     data[2], data[3], data[4]);
            }
            // 送进去；满了就先取帧再重送
            while (true) {
                const FeedResult feedResult = decoder.feedPacket(
                        annexbPacketPtr->data, static_cast<size_t>(annexbPacketPtr->size), ptsUs);
                if (feedResult == FeedResult::Sent) {
                    break;
                }
                if (feedResult == FeedResult::Error) {
                    return 1;
                }
                if (!drainFramesToFile(decoder, outputFilePtr.get(), maxFrameCount, writtenFrameCount, hasReachedEos)) {
                    return 1;
                }
                sleepForUs(1000);
            }
        }
        if (!drainFramesToFile(decoder, outputFilePtr.get(), maxFrameCount, writtenFrameCount, hasReachedEos)) {
            return 1;
        }
    }

    // 第 3 步 送 EOS，取完剩下的帧（限制了帧数时就不等了）
    if (maxFrameCount <= 0) {
        while (decoder.feedEndOfStream() == FeedResult::Full) {
            drainFramesToFile(decoder, outputFilePtr.get(), maxFrameCount, writtenFrameCount, hasReachedEos);
            sleepForUs(1000);
        }
        int64_t lastProgressTimeUs = getSteadyTimeUs();
        while (!hasReachedEos && getSteadyTimeUs() - lastProgressTimeUs < 2 * 1000 * 1000) {
            const int frameCountBefore = writtenFrameCount;
            if (!drainFramesToFile(decoder, outputFilePtr.get(), maxFrameCount, writtenFrameCount, hasReachedEos)) {
                return 1;
            }
            if (writtenFrameCount != frameCountBefore) {
                lastProgressTimeUs = getSteadyTimeUs();
            }
            sleepForUs(1000);
        }
    }

    // 第 4 步 统计
    const double elapsedSeconds = (getSteadyTimeUs() - startTimeUs) / 1e6;
    LOGI(kTag, "视频包 %d 个，写出 %d 帧，EOS %s，info change %d 次，用时 %.2f 秒（%.1f fps）→ %s", videoPacketCount,
         writtenFrameCount, hasReachedEos ? "收到" : "没收到", decoder.getInfoChangeCount(), elapsedSeconds,
         writtenFrameCount / elapsedSeconds, outputPath);
    return 0;
}
