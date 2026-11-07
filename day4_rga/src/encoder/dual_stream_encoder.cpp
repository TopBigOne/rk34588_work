//
// DualStreamEncoder 的实现。write_packet、init_encoder_and_write_header、drain_encoder 从 main.cpp 搬过来，逻辑没改
//
#include "encoder/dual_stream_encoder.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include "common/log.h"
#include "common/time_utils.h"
#include "rga/rga_resizer.h"

static const char *kTag = "DualStream";

/**
 * @brief 把一个编码包写进码流文件
 * @param[in] streamFile     码流文件
 * @param[in] encodedPacket  encode() 拿到的包；data 为空（编码器这次没出包）时什么都不写
 * @return true = 写完整了或者不用写；false = fwrite 没写完（比如磁盘满了）
 */
static bool write_packet(FILE *streamFile, const EncodedPacket &encodedPacket) {
    if (encodedPacket.data.empty()) {
        return true;
    }
    return fwrite(encodedPacket.data.data(), 1, encodedPacket.data.size(), streamFile) == encodedPacket.data.size();
}

/**
 * @brief 初始化编码器，并把 SPS/PPS 写到码流文件开头
 * @param[in]  encoderArg  编码参数
 * @param[in]  streamFile  码流文件（刚打开、还是空的）
 * @param[out] encoder     要初始化的编码器
 * @return true = 成功；false = init、取 header 或写文件失败
 * @note 每路码流都要自己的 SPS/PPS：里面有宽高、profile、level，两路不一样
 */
static bool init_encoder_and_write_header(const EncoderConfigArg &encoderArg, FILE *streamFile, MppEncoder &encoder) {
    if (!encoder.init(encoderArg)) {
        return false;
    }
    std::vector<uint8_t> header;
    if (!encoder.getHeader(header)) {
        return false;
    }
    return fwrite(header.data(), 1, header.size(), streamFile) == header.size();
}

/**
 * @brief 排空编码器：告诉编码器"没有新帧了"，把它内部还没输出的码流全部取出来写进文件，直到收到带 EOS 标志的包
 * @param[in] encoder     已经 init 过、送完所有帧的编码器
 * @param[in] streamFile  码流文件，取出来的包追加写在后面
 * @return true = 收到 EOS 包（或者送了 30 次还没收到，只打 warning）；false = encode 或写文件失败
 * @note 现在的配置（只有 I/P 帧、put 一帧就阻塞 get 一包）下，2026-11-06 实测 drain 取出 0 字节；
 *       开了 B 帧或多线程编码时，编码器内部会积压几帧，必须排空才不丢尾帧
 */
static bool drain_encoder(MppEncoder &encoder, FILE *streamFile) {
    EncodedPacket encodedPacket;
    // 最多送 30 次：防止编码器一直不回 EOS 包时死循环
    for (int tryIndex = 0; tryIndex < 30; ++tryIndex) {
        // ① 送一个"空帧 + EOS 标志"：没有图像，只表示"输入结束了"；同时取一个包
        if (!encoder.encode(nullptr, true, encodedPacket)) {
            return false;
        }
        // ② 取出来的包写进文件：可能是编码器内部剩下的码流，也可能是空包
        if (!write_packet(streamFile, encodedPacket)) {
            return false;
        }
        // ③ 包上带 EOS 标志：编码器表示"内部的码流已经全部输出完了"
        if (encodedPacket.isEos) {
            return true;
        }
    }
    LOGW(kTag, "送了 30 次 EOS 还没拿到 EOS 包");
    return true;
}

bool DualStreamEncoder::open(const EncoderConfigArg &mainCfg, const char *mainStreamPath,
                             const EncoderConfigArg &subCfg, const char *subStreamPath) {
    // 第 1 步 子码流 buffer：从 DRM 类型的 group 申请，RGA 和编码器都能通过 fd 用它；宽高、stride 和 subCfg 一致
    MppBufferGroup rawGroup = nullptr;
    if (mpp_buffer_group_get_internal(&rawGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return false;
    }
    bufferGroup_.reset(rawGroup); // 交给 MppBufferGroupPtr，析构时自动 mpp_buffer_group_put
    subPicture_ = {.mppBuf = nullptr,
                   .width = subCfg.width,
                   .height = subCfg.height,
                   .horStride = subCfg.horStride,
                   .verStride = subCfg.verStride};
    subBufOwner_ = alloc_nv12_buffer(bufferGroup_.get(), subPicture_);
    if (!subBufOwner_) {
        return false;
    }

    // 第 2 步 打开两个码流文件
    mainStreamFile_.reset(fopen(mainStreamPath, "wb"));
    subStreamFile_.reset(fopen(subStreamPath, "wb"));
    if (!mainStreamFile_ || !subStreamFile_) {
        LOGE(kTag, "打开码流文件失败：%s", strerror(errno));
        return false;
    }

    // 第 3 步 两个编码器各自 init，各自把 SPS/PPS 写到自己文件的开头
    return init_encoder_and_write_header(mainCfg, mainStreamFile_.get(), mainEncoder_)
    && init_encoder_and_write_header(subCfg, subStreamFile_.get(), subEncoder_);
}

bool DualStreamEncoder::encode_main_and_resize_to_sub(const Nv12Picture &mainPicture) {
    // 主码流：编码器 #1 直接编输入画面（M3 里就是摄像头的 buffer，零拷贝）
    if (!mainEncoder_.encode(mainPicture.mppBuf, false, mainPacket_) ||
        !write_packet(mainStreamFile_.get(), mainPacket_)) {
        return false;
    }
    mainStreamBytes_ += mainPacket_.data.size();

    // 子码流的输入：RGA 读输入画面，缩好写进 subPicture_
    const double beforeResizeMs = get_now_ms();
    const bool isResizeOk = resize_nv12_by_rga(mainPicture, subPicture_);
    resizeTotalMs_ += get_now_ms() - beforeResizeMs;
    return isResizeOk;
}

bool DualStreamEncoder::encode_sub() {
    if (!subEncoder_.encode(subPicture_.mppBuf, false, subPacket_) ||
        !write_packet(subStreamFile_.get(), subPacket_)) {
        return false;
    }
    subStreamBytes_ += subPacket_.data.size();
    return true;
}

bool DualStreamEncoder::drain_both() {
    return drain_encoder(mainEncoder_, mainStreamFile_.get()) && drain_encoder(subEncoder_, subStreamFile_.get());
}
