#include <cstdio>
#include <iostream>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
#include <vector>
#define FUNC_TAG "MPI_DEC_MOST_LIGHT"
#define msleep(x) usleep((x) * 1000)
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        MPP_RET _r = (expr);                                                                                           \
        if (_r != MPP_OK) {                                                                                            \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__);                                         \
            goto CLEANUP;                                                                                              \
        }                                                                                                              \
    } while (0)

MPP_RET initDecoderConfig(MppApi *mpp_api, MppCtx mpp_ctx, MppDecCfg dec_cfg);

static bool write_nv12_frame(FILE *fp, MppFrame frame) {
    puts("write_nv12_frame");
    const RK_U32 width = mpp_frame_get_width(frame); // 1920
    const RK_U32 height = mpp_frame_get_height(frame); // 1080
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame); // 1920
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame); // 1088
    MppBuffer buffer = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    // 便于进行指针运算
    const auto *base = static_cast<const uint8_t *>(mpp_buffer_get_ptr(buffer));
    printf("case 1: base address : %p\n", base);
    for (RK_U32 row = 0; row < height; row++) { // Y：1080 行
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    printf("case 2: base address : %p\n", base);
    const uint8_t *uv = base + horStride * verStride; // UV 从第 1088 行开始
    for (RK_U32 row = 0; row < height / 2; row++) { // UV：540 行
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}
/*
 * main - 程序入口
 *
 * 使用方法示例：
 *   mpi_dec_test -i input.h264 -t 7 -o output.yuv -n 100
 *     -i: 输入码流文件
 *     -t: 编码类型（7=H.264, 16777220=H.265, 参见 MppCodingType）
 *     -o: 输出 YUV 文件（可选）
 *     -n: 解码帧数（-1=无限循环, 0=解到文件结束, >0=指定帧数）
 *     -w/-h: 视频宽高（JPEG 模式必须指定）
 */
int main(int argc, char **argv) {
    MPP_RET ret = MPP_NOK;
    MppCtx mpp_ctx = NULL;
    MppApi *mpp_api = NULL;
    MppDecCfg decCfg = NULL;
    const char *inputPath = "/userdata/av/aaa.264";
    const char *outPath = "/userdata/av/out.nv12";
    const size_t chunkSize = 64 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    FILE *inputFile = NULL;
    MppPacket inputPacket = NULL;
    bool inputEos = false;

    int gotInfoChange = 0;
    MppBufferGroup bufferGroup = NULL;

    FILE *outputFile = NULL;
    MppFrame outputFrame = NULL;
    bool outputEos = false;
    int exitCode = -1;
    RK_S32 decodedFrameCount = 0;
    RK_S32 maxFrames = 10;
    bool reachedLimit = false;
    int readSize = 0;
    // step 1:
    ret = mpp_create(&mpp_ctx, &mpp_api);
    CHECK(ret);


    // step 2: h264 视频解码器
    ret = mpp_init(mpp_ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    CHECK(ret);
    ret = mpp_dec_cfg_init(&decCfg);
    CHECK(ret);
    initDecoderConfig(mpp_api, mpp_ctx, decCfg);
    puts("decoder ready");

    inputFile = fopen(inputPath, "rb");
    if (!inputFile) {
        printf("open %s failed\n", inputPath);
        goto CLEANUP;
    }
    outputFile = fopen(outPath, "wb");
    if (!outputFile) {
        printf("open %s failed\n", outPath);
        goto CLEANUP;
    }

    while (!outputEos && !reachedLimit) {
        if (!inputPacket && !inputEos) {
            readSize = fread(chunk.data(), 1, chunkSize, inputFile);
            inputEos = readSize < chunkSize;
            // 1:将数据包，封装为packet
            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));
            if (inputEos) {
                mpp_packet_set_eos(inputPacket);
            }
        }
        // 2: 送给解码器
        if (inputPacket) {
            MPP_RET ret = mpp_api->decode_put_packet(mpp_ctx, inputPacket);
            if (ret == MPP_OK) {
                mpp_packet_deinit(&inputPacket);
            } else if (ret == MPP_ERR_BUFFER_FULL) {
                //队列满：inputPacket 留着，等 1ms 后重送（正常的流量控制，不是错误）
                //  printf("[warning] decode_put_packet : %s\n", "buffer is full");
            } else {
                printf("[FAIL] decode_put_packet ret=%d\n", ret);
                goto CLEANUP;
            }
        }

        // 3: 得到解码的帧
        while (true) {
            ret = mpp_api->decode_get_frame(mpp_ctx, &outputFrame);
            CHECK(ret);
            if (!outputFrame) {
                break;
            }

            if (outputFrame && mpp_frame_get_info_change(outputFrame)) {
                RK_U32 width = mpp_frame_get_width(outputFrame);
                RK_U32 height = mpp_frame_get_height(outputFrame);
                RK_U32 horStride = mpp_frame_get_hor_stride(outputFrame);
                RK_U32 verStride = mpp_frame_get_ver_stride(outputFrame);
                RK_U32 bufSize = mpp_frame_get_buf_size(outputFrame);
                printf("|            info change: %ux%u stride %ux%u buf_size %u\n", width, height, horStride,
                       verStride, bufSize);
                // 建内存池
                ret = mpp_buffer_group_get_internal(&bufferGroup, MPP_BUFFER_TYPE_DRM);
                CHECK(ret);
                ret = mpp_buffer_group_limit_config(bufferGroup, bufSize, 24);
                CHECK(ret);
                mpp_api->control(mpp_ctx, MPP_DEC_SET_EXT_BUF_GROUP, bufferGroup);
                mpp_api->control(mpp_ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
                gotInfoChange = true;
            } else {
                const RK_U32 errInfo = mpp_frame_get_errinfo(outputFrame);
                const RK_U32 discard = mpp_frame_get_discard(outputFrame);
                if (errInfo || discard) {
                    printf("               err %x discard %x, skip\n", errInfo, discard);
                } else if (mpp_frame_get_buffer(outputFrame)) {
                    bool writeFrameResult = write_nv12_frame(outputFile, outputFrame);
                    if (!writeFrameResult) {
                        perror(" fwrite frame in error");
                        goto CLEANUP;
                    }
                    decodedFrameCount++;
                    if (decodedFrameCount >= maxFrames) {
                        reachedLimit = true;
                    }
                }
            }

            outputEos = mpp_frame_get_eos(outputFrame);
            mpp_frame_deinit(&outputFrame);
            if (outputEos || reachedLimit) {
                break;
            }
        }

        if (inputPacket || (inputEos && !outputEos && !reachedLimit)) {
            // 等1ms
            usleep(1000);
        }
    }
    exitCode = 0;

CLEANUP:
    if (outputFrame) {
        mpp_frame_deinit(&outputFrame);
    }

    if (inputPacket) {
        mpp_packet_deinit(&inputPacket);
    }

    if (decCfg)
        mpp_dec_cfg_deinit(decCfg); // 先释放 cfg

    if (mpp_ctx) {
        mpp_destroy(mpp_ctx); // 再销毁解码器
    }

    if (bufferGroup) {
        mpp_buffer_group_put(bufferGroup);
    }

    if (inputFile) {
        fclose(inputFile);
    }
    if (outputFile) {
        fclose(outputFile);
    }

    return exitCode;
}

MPP_RET initDecoderConfig(MppApi *mpp_api, MppCtx mpp_ctx, MppDecCfg dec_cfg) {
    MPP_RET ret = mpp_api->control(mpp_ctx, MPP_DEC_GET_CFG, dec_cfg);
    if (ret != MPP_OK)
        return ret;
    ret = mpp_dec_cfg_set_u32(dec_cfg, "base:split_parse", 1);
    if (ret != MPP_OK)
        return ret;
    ret = mpp_api->control(mpp_ctx, MPP_DEC_SET_CFG, dec_cfg);
    if (ret != MPP_OK)
        return ret;
    return MPP_OK;
}
