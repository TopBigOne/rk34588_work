#include "video/annexb_converter.h"

#include <utility> // std::move

#include "common/log.h"

static const char *const kTag = "AnnexbConverter";

/**
 * @brief 选 filter 并初始化
 * @param[in] videoStream  视频流
 * @return true = 成功；false = 失败
 * @note 第 1 步 按格式选名字；第 2 步 申请实例；第 3 步 复制流参数（里面有 avcC）；第 4 步 初始化
 */
bool AnnexbConverter::init(const AVStream *videoStream) {
    if (bsfContextPtr) {
        LOGE(kTag, "init 只能调用一次");
        return false;
    }

    // 第 1 步 按编码格式选 filter 的名字
    const char *filterName = nullptr;
    if (videoStream->codecpar->codec_id == AV_CODEC_ID_H264) {
        filterName = "h264_mp4toannexb";
    } else if (videoStream->codecpar->codec_id == AV_CODEC_ID_HEVC) {
        filterName = "hevc_mp4toannexb";
    } else {
        LOGE(kTag, "只支持 H.264 / H.265，这个文件是 %s", avcodec_get_name(videoStream->codecpar->codec_id));
        return false;
    }
    // av_bsf_get_by_name：按名字找 filter 的"类型描述"（静态的，不用释放）；板子的 ffmpeg 编译时没带这个 filter 就返回 nullptr
    const AVBitStreamFilter *filter = av_bsf_get_by_name(filterName);
    if (!filter) {
        LOGE(kTag, "ffmpeg 里没有 %s", filterName);
        return false;
    }

    // 第 2 步 按类型申请一个实例
    AVBSFContext *rawBsfContext = nullptr;
    int ret = av_bsf_alloc(filter, &rawBsfContext);
    if (ret < 0) {
        LOGE(kTag, "av_bsf_alloc 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    bsfContextPtr.reset(rawBsfContext);

    // 第 3 步 告诉 filter 输入是什么样的：codecpar 里的 extradata 就是 mp4 文件头里的 avcC / hvcC，
    // SPS/PPS 就是从这里拿的；time_base_in 是输入包的时间单位（filter 不改时间戳，只是记一下）
    ret = avcodec_parameters_copy(bsfContextPtr->par_in, videoStream->codecpar);
    if (ret < 0) {
        LOGE(kTag, "avcodec_parameters_copy 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    bsfContextPtr->time_base_in = videoStream->time_base;

    // 第 4 步 初始化：filter 在这里解析 avcC，准备好要插进码流的 SPS/PPS
    ret = av_bsf_init(bsfContextPtr.get());
    if (ret < 0) {
        LOGE(kTag, "av_bsf_init 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }
    LOGI(kTag, "%s 初始化完成，avcC/hvcC %d 字节", filterName, videoStream->codecpar->extradata_size);
    return true;
}

/**
 * @brief 转换一个包
 * @param[in]  inputPacketPtr  AVCC 包
 * @param[out] outputPackets   转好的包追加到这里
 * @return true = 成功；false = 失败
 * @note 第 1 步 送进去；第 2 步 循环取出来，直到 EAGAIN（"要更多输入"）
 */
bool AnnexbConverter::convert(AvPacketPtr inputPacketPtr, std::vector<AvPacketPtr> &outputPackets) {
    // 第 1 步 av_bsf_send_packet：filter 接管包里的数据引用，并把我们的 AVPacket 清空（结构体还要我们释放，
    // inputPacketPtr 在函数结束时自动释放）
    int ret = av_bsf_send_packet(bsfContextPtr.get(), inputPacketPtr.get());
    if (ret < 0) {
        LOGE(kTag, "av_bsf_send_packet 失败：%s", convertFfmpegErrorToString(ret).c_str());
        return false;
    }

    // 第 2 步 av_bsf_receive_packet：取一个转好的包。
    // 返回 AVERROR(EAGAIN) 表示"这次送进去的都取完了，要再送才有"，不是错误
    while (true) {
        AvPacketPtr outputPacketPtr = allocateAvPacket();
        if (!outputPacketPtr) {
            LOGE(kTag, "av_packet_alloc 失败");
            return false;
        }
        ret = av_bsf_receive_packet(bsfContextPtr.get(), outputPacketPtr.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            LOGE(kTag, "av_bsf_receive_packet 失败：%s", convertFfmpegErrorToString(ret).c_str());
            return false;
        }
        outputPackets.push_back(std::move(outputPacketPtr));
    }
}

/**
 * @brief 清掉 filter 里剩下的包
 * @return 无
 * @note av_bsf_flush 之后 filter 回到刚初始化完的状态
 */
void AnnexbConverter::flush() {
    if (bsfContextPtr) {
        av_bsf_flush(bsfContextPtr.get());
    }
}
