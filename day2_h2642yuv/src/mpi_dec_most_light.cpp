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
    const size_t chunkSize = 64 * 1024;
    std::vector<uint8_t> chunk(chunkSize);
    FILE *inputFile = NULL;
    int gotInfoChange = 0;
    MppBufferGroup bufferGroup = NULL;
    MppPacket inputPacket = NULL;
    MppFrame outFrame = NULL;
    int exitCode = -1;
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

    while (!gotInfoChange) {
        if (!inputPacket) {
            readSize = fread(chunk.data(), 1, chunkSize, inputFile);
            if (readSize == 0) {
                printf("文件读完了还没等到 info change\n");
                goto CLEANUP;
            }
            // 1:将数据包，封装为packet
            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));
        }
        // 2: 送给解码器
        MPP_RET ret = mpp_api->decode_put_packet(mpp_ctx, inputPacket);
        if (ret == MPP_OK) {
            mpp_packet_deinit(&inputPacket);
        }
        // 3: 得到解码的帧
        ret = mpp_api->decode_get_frame(mpp_ctx, &outFrame);
        CHECK(ret);
        if (outFrame && mpp_frame_get_info_change(outFrame)) {
            RK_U32 width = mpp_frame_get_width(outFrame);
            RK_U32 height = mpp_frame_get_height(outFrame);
            RK_U32 horStride = mpp_frame_get_hor_stride(outFrame);
            RK_U32 verStride = mpp_frame_get_ver_stride(outFrame);
            RK_U32 bufSize = mpp_frame_get_buf_size(outFrame);
            printf("|            info change: %ux%u stride %ux%u buf_size %u\n", width, height, horStride, verStride,
                   bufSize);
            // 建内存池
            ret = mpp_buffer_group_get_internal(&bufferGroup, MPP_BUFFER_TYPE_DRM);
            CHECK(ret);
            ret = mpp_buffer_group_limit_config(bufferGroup, bufSize, 24);
            CHECK(ret);
            mpp_api->control(mpp_ctx, MPP_DEC_SET_EXT_BUF_GROUP, bufferGroup);
            mpp_api->control(mpp_ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
            gotInfoChange = true;
        }
        if (outFrame) {
            mpp_frame_deinit(&outFrame);
        }
        if (!gotInfoChange) {
            // 等1ms
            usleep(1000);
        }
    }
    exitCode = 0;

CLEANUP:
    if (outFrame) {
        mpp_frame_deinit(&outFrame);
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
