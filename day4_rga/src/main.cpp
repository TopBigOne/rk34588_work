#include <iostream>


/**
 * RGA 缩放+ MPP 双码流
 * --
 * RAG：[rga_resizer.cpp]
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
static const char *inputFilePath = "/userdata/av/in_1080p_60f.nv12";
static const char *kResizedFilePath = "/userdata/av/out_720p.nv12"; // M1 的输出
static const char *kMainH264StreamPath = "/userdata/av/main_1080p.h264"; // M2 主码流
static const char *kSubH264StreamPath = "/userdata/av/sub_720p.h264"; // M2 子码流

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


/**
 * Q1 :每一圈的 frameStart 一样吗？→ 一样;
 * Q2 :写新内容，能把上一次的数据全部擦掉吗？→ 画面部分全部覆盖，填充部分不碰;
 * 地址不变；画面字节每圈全部覆盖，填充字节不碰也不需要碰；真正可能让上一帧“残留”的是 CPU cache，靠 sync_end 写回 DRAM
 * 来解决。
 * @param inputFile
 * @param picture
 * @return
 */
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
 * @brief 排空编码器：告诉编码器"没有新帧了"，把它内部还没输出的码流全部取出来写进文件，直到收到带 EOS 标志的包
 * @param[in] encoder     已经 init 过、送完所有帧的编码器
 * @param[in] streamFile  码流文件，取出来的包追加写在后面
 * @return true = 收到 EOS 包（或者送了 30 次还没收到，只打 warning）；false = encode 或写文件失败
 * @note 送完最后一帧后调用一次。现在的配置（只有 I/P 帧、put 一帧就阻塞 get 一包）下，
 *       2026-11-06 实测 drain 取出 0 字节；开了 B 帧或多线程编码时，编码器内部会积压几帧，必须排空才不丢尾帧
 */
static bool drain_encoder(MppEncoder &encoder, FILE *streamFile) {
    EncodedPacket encodedPacket;
    // 最多送 30 次：防止编码器一直不回 EOS 包时死循环
    for (int tryIndex = 0; tryIndex < 30; ++tryIndex) {
        // ① 送一个"空帧 + EOS 标志"：没有图像，只表示"输入结束了"；同时取一个包
        if (!encoder.encode(nullptr, true, encodedPacket)) {
            return false;
        }
        // ② 这次取出来的包写进文件：可能是编码器内部剩下的码流，也可能是空包（data 为空时 write_packet 什么都不写）
        if (!write_packet(streamFile, encodedPacket)) {
            return false;
        }
        // ③ 包上带 EOS 标志：编码器表示"内部的码流已经全部输出完了"，排空完成
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
    MppBufferGroup bufferGroup = nullptr;
    if (mpp_buffer_group_get_internal(&bufferGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }
    // 输入 1080p：verStride 用 ALIGN(1080, 16) = 1088
    Nv12Picture srcPicture = {nullptr, kMainWidth, kMainHeight, ALIGN(kMainWidth, 16), ALIGN(kMainHeight, 16)};

    // 输出 720p：1280、720
    Nv12Picture dstPicture = {nullptr, kSubWidth, kSubHeight, ALIGN(kSubWidth, 16), ALIGN(kSubHeight, 16)};

    FILE *inputFile = fopen(inputFilePath, "rb");
    FILE *outputFile = fopen(kResizedFilePath, "wb");
    int exitCode = 1;
    if (inputFile == nullptr || outputFile == nullptr) {

    } else if (alloc_nv12_picture(bufferGroup, srcPicture) && alloc_nv12_picture(bufferGroup, dstPicture) &&
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
    mpp_buffer_group_put(bufferGroup);
    return exitCode;
}

/**
 * 文件双码流。
 * 每一帧：原图-> 编码器#1(1080p);
 *              RGA 缩小成720p->编码器#2（720p）
 * @return
 */
static int run_m2_dual_stream_from_file() {
    LOGI(kTag, "===== M2：文件双码流（1080p 主码流 + 720p 子码流）=====");
    MppBufferGroup mppBufferGroup = nullptr;
    if (mpp_buffer_group_get_internal(&mppBufferGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE) != MPP_OK) {
        LOGE(kTag, "mpp_buffer_group_get_internal 失败");
        return 1;
    }

    Nv12Picture pictureMain = {.mppBuf = nullptr,
                               .width = kMainWidth,
                               .height = kMainHeight,
                               .horStride = ALIGN(kMainWidth, 16),
                               .verStride = ALIGN(kMainHeight, 16)};

    Nv12Picture pictureSub = {.mppBuf = nullptr,
                              .width = kSubWidth,
                              .height = kSubHeight,
                              .horStride = ALIGN(kSubWidth, 16),
                              .verStride = ALIGN(kSubHeight, 16)};

    EncoderConfigArg mainCfg;
    mainCfg.width = pictureMain.width;
    mainCfg.height = pictureMain.height;
    mainCfg.horStride = pictureMain.horStride;
    mainCfg.verStride = pictureMain.verStride;
    mainCfg.bps = 4 * 1000 * 1000;

    EncoderConfigArg subCfg;
    subCfg.width = pictureSub.width;
    subCfg.height = pictureSub.height;
    subCfg.horStride = pictureSub.horStride;
    subCfg.verStride = pictureSub.verStride;
    subCfg.bps = 1500 * 1000;

    // open file
    FILE *inputFile = fopen(inputFilePath, "rb");
    FILE *mainStreamFile = fopen(kMainH264StreamPath, "wb");
    FILE *subStreamFile = fopen(kSubH264StreamPath, "wb");

    MppEncoder mainEncoder;
    MppEncoder subEncoder;
    int exitCode = 1;

    if (inputFile == nullptr || mainStreamFile == nullptr || subStreamFile == nullptr) {
        LOGE(kTag, "打开文件失败：%s", strerror(errno));
        if (inputFile != nullptr)
            fclose(inputFile);
        if (mainStreamFile != nullptr)
            fclose(mainStreamFile);
        if (subStreamFile != nullptr)
            fclose(subStreamFile);
        return exitCode;
    }

    bool allocNv12PictureMain = alloc_nv12_picture(mppBufferGroup, pictureMain);
    bool allocNv12PictureSub = alloc_nv12_picture(mppBufferGroup, pictureSub);
    bool initHeadMain = init_encoder_and_write_header(mainCfg, mainStreamFile, mainEncoder);
    bool initHeadSub = init_encoder_and_write_header(subCfg, subStreamFile, subEncoder);

    if (allocNv12PictureMain && allocNv12PictureSub && initHeadMain && initHeadSub) {
        int frameCount = 0;
        size_t mainStreamBytes = 0;
        size_t subStreamBytes = 0;
        double resizeTotalMs = 0;
        bool isAllOk = true;
        EncodedPacket mainPacket;
        EncodedPacket subPacket;

        // 前面都没出错，并且还能再读出一整帧，就继续处理这一帧。
        while (isAllOk && read_nv12_frame(inputFile, pictureMain)) {
            // case 1: main
            isAllOk = mainEncoder.encode(pictureMain.mppBuf, false, mainPacket) &&
                      write_packet(mainStreamFile, mainPacket);
            const double beforeResizeMs = get_now_ms();
            isAllOk = isAllOk && resize_nv12_by_rga(pictureMain, pictureSub);
            resizeTotalMs += get_now_ms() - beforeResizeMs;

            // case 2: sub
            isAllOk = isAllOk && subEncoder.encode(pictureSub.mppBuf, false, subPacket) &&
                      write_packet(subStreamFile, subPacket);
            mainStreamBytes += mainPacket.data.size();
            subStreamBytes += subPacket.data.size();
            ++frameCount;
        }

        if (isAllOk && drain_encoder(mainEncoder, mainStreamFile) && drain_encoder(subEncoder, subStreamFile)) {
            LOGI(kTag, "%d 帧；RGA 缩放平均 %.2f ms/帧", frameCount, frameCount > 0 ? resizeTotalMs / frameCount : 0);
            LOGI(kTag, "主码流 %dx%d：%zu 字节 -> %s", mainCfg.width, mainCfg.height, mainStreamBytes,
                 kMainH264StreamPath);
            LOGI(kTag, "子码流 %dx%d：%zu 字节 -> %s", subCfg.width, subCfg.height, subStreamBytes, kSubH264StreamPath);
            exitCode = 0;
        }
    }

    if (pictureMain.mppBuf != nullptr) {
        mpp_buffer_put(pictureMain.mppBuf);
    }
    if (pictureSub.mppBuf != nullptr) {
        mpp_buffer_put(pictureSub.mppBuf);
    }

    for (FILE *file: {inputFile, mainStreamFile, subStreamFile}) {
        if (file != nullptr) {
            fclose(file);
        }
    }
    mpp_buffer_group_put(mppBufferGroup);
    return exitCode;
}


int main(int argc, char **argv) {
    const char *experimentName = argc > 1 ? argv[1] : "m2";
    if (strcmp(experimentName, "m1") == 0) {
        return run_m1_resize_one_frame();
    }
    if (strcmp(experimentName, "m2") == 0) {
        return run_m2_dual_stream_from_file();
    }

    LOGE(kTag, "用法：%s m1 | m2", argv[0]);
    return 1;
}
