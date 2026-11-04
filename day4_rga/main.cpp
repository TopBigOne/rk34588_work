#include <iostream>


/**
 * RGA 缩放+ MPP 双码流
 * @return
 */

#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>
#include "common/log.h"
#include "common/mpp_utils.h"
#include "encoder/mpp_encoder.h"
#include "rga/rga_resizer.h"

static const char *const kTag = "Main";
// 输入：Day 1 用过的 60 帧 1080p NV12 文件（每帧紧挨着存，没有填充）
static const char *kInputFilePath = "/userdata/av/in_1080p_60f.nv12";
static const char *kResizedFilePath = "/userdata/av/out_720p.nv12"; // M1 的输出
static const char *kMainStreamPath = "/userdata/av/main_1080p.h264"; // M2 主码流
static const char *kSubStreamPath = "/userdata/av/sub_720p.h264"; // M2 子码流

static const int kMainWidth = 1920;
static const int kMainHeight = 1080;
static const int kSubWidth = 1280;
static const int kSubHeight = 720;

/**
 * @brief 取当前时间，单位毫秒（只用来算两次之间的差）
 * @return 从某个固定起点开始的毫秒数
 */
static double get_now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}


static bool alloc_nv12_picture(MppBufferGroup bufGroup, Nv12Picture &picture) {
    // nv21 一帧的大小
    const size_t frameSizeBytes = static_cast<size_t>(picture.horStride) * picture.verStride * 3 / 2;
    if (mpp_buffer_get(bufGroup, &picture.mppBuf, frameSizeBytes) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_get %zu 字节失败", frameSizeBytes);
        return false;
    }
    return true;
}


static bool read_nv12_frame(FILE *inputFile, const Nv12Picture &picture) {
    uint8_t *frameStart = static_cast<uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    // cpu 要写cached的buffer，写之前需要sync_begin(),写之后sync_end(),保证RGA、编码器读到的是cpu刚写的
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isReadOk = true;
    // Y 平面：height 行
    for (int rowIndex = 0; rowIndex < picture.height && isReadOk; ++rowIndex) {
        isReadOk = fread(frameStart + rowIndex * picture.horStride, 1, picture.width, inputFile) ==
                   static_cast<size_t>(picture.width);
    }
    // UV 平面：从第 verStride 行开始，height / 2 行
    uint8_t *uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isReadOk; ++rowIndex) {
        isReadOk = fread(uvStart + rowIndex * picture.horStride, 1, picture.width, inputFile) ==
                   static_cast<size_t>(picture.width);
    }

    mpp_buffer_sync_end(picture.mppBuf);
    return isReadOk;
}

static bool write_nv12_frame(FILE *outputFile, const Nv12Picture &picture) {
    auto frameStart = static_cast<const uint8_t *>(mpp_buffer_get_ptr(picture.mppBuf));
    mpp_buffer_sync_begin(picture.mppBuf);
    bool isWriteOk = true;
    for (int rowIndex = 0; rowIndex < picture.height && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(frameStart + rowIndex * picture.horStride, 1, picture.width, outputFile) ==
                    static_cast<size_t>(picture.width);
    }
    const uint8_t *uvStart = frameStart + picture.horStride * picture.verStride;
    for (int rowIndex = 0; rowIndex < picture.height / 2 && isWriteOk; ++rowIndex) {
        isWriteOk = fwrite(uvStart + rowIndex * picture.horStride, 1, picture.width, outputFile) ==
                    static_cast<size_t>(picture.width);
    }

    mpp_buffer_sync_end(picture.mppBuf);
    return isWriteOk;
}

static bool write_packet(FILE *streamFile, const EncodedPacket &encodedPacket) {
    if (encodedPacket.data.empty()) {
        return true;
    }
    return fwrite(encodedPacket.data.data(), 1, encodedPacket.data.size(), streamFile) == encodedPacket.data.size();
}

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
 * 给编码器送EOS
 * @param encoder
 * @param streamFile
 * @return
 */
static bool drain_encoder(MppEncoder &encoder, FILE *streamFile) {
    EncodedPacket encodedPacket;
    for (int tryIndex = 0; tryIndex < 30; ++tryIndex) {
        if (encoder.encode(nullptr, true, encodedPacket) != MPP_OK) {
            return false;
        }
        write_packet(streamFile, encodedPacket);
        if (encodedPacket.isEos) {
            return true;
        }
    }
    LOGW(kTag, "送了 30 次 EOS 还没拿到 EOS 包");
    return true;
}

/**
 * 读一帧1080--> RGA-->缩成720 写成NV12文件
 * @return
 */
static int run_m1_resize_one_frame() {
    LOGI(kTag, "===== M1：RGA 把一帧 1080p 缩成 720p =====");
    MppBufferGroup buffer_group = nullptr;
    if (mpp_buffer_group_get_internal(&buffer_group, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    // 输入 1080p：verStride 用 ALIGN(1080, 16) = 1088
    Nv12Picture srcPicture = {nullptr, kMainWidth, kMainHeight, ALIGN(kMainWidth, 16), ALIGN(kMainHeight, 16)};

    // 输出 720p：1280、720
    Nv12Picture dstPicture = {nullptr, kSubWidth, kSubHeight, ALIGN(kSubWidth, 16), ALIGN(kSubHeight, 16)};

    FILE *inputFile = fopen(kInputFilePath, "rb");
    FILE *outputFile = fopen(kResizedFilePath, "wb");
    int exitCode = 1;
    if (inputFile == nullptr || outputFile == nullptr) {

    } else if (alloc_nv12_picture(buffer_group, srcPicture) && alloc_nv12_picture(buffer_group, dstPicture) &&
               read_nv12_frame(inputFile, srcPicture)) {


        const double beforeResizeMs = get_now_ms();
        bool isResizeOk = resize_nv12_by_rga(srcPicture, dstPicture);
        const double resizeMs = get_now_ms() - beforeResizeMs;
        if (isResizeOk && write_nv12_frame(outputFile, dstPicture)) {
            LOGI(kTag, "%dx%d（stride %dx%d）-> %dx%d（stride %dx%d），RGA 用时 %.2f ms，写到 %s", srcPicture.width,
                 srcPicture.height, srcPicture.horStride, srcPicture.verStride, dstPicture.width, dstPicture.height,
                 dstPicture.horStride, dstPicture.verStride, resizeMs, kResizedFilePath);
            exitCode = 0;
        }
    }

    if (srcPicture.mppBuf != nullptr) {
        mpp_buffer_put(srcPicture.mppBuf);
    }
    if (dstPicture.mppBuf != nullptr) {
        mpp_buffer_put(dstPicture.mppBuf);
    }
    if (inputFile != nullptr) {
        fclose(inputFile);
    }
    if (outputFile != nullptr) {
        fclose(outputFile);
    }
    mpp_buffer_group_put(buffer_group);
    return exitCode;
}

int main(int argc, char **argv) {
    const char *experimentName = argc > 1 ? argv[1] : "m1";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_resize_one_frame();
    }

    LOGE(kTag, "用法：%s m1 | m2", argv[0]);
    return 1;
}
