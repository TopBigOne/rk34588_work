#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
#include <vector>
#define FUNC_TAG  "MPI_DEC_MOST_LIGHT"
#define msleep(x) usleep((x) * 1000)
#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

MPP_RET initDecoderConfig(MppApi* mpp_api, MppCtx mpp_ctx, MppDecCfg dec_cfg);

typedef struct Args {
    const char* streamInputPath = "/userdata/av/aaa.264";
    const char* yuvOutputPath   = nullptr;
    MppCodingType type          = MPP_VIDEO_CodingAVC;
    int maxFrames               = 0;
} Args;

static bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i += 2) {
        const char* opt = argv[i];
        const char* val = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!val) {
            printf("参数 %s 缺少值\n", opt);
            return false;
        }
        if (!strcmp(opt, "-i")) {
            a.streamInputPath = val;
        } else if (!strcmp(opt, "-o")) {
            a.yuvOutputPath = val;
        } else if (!strcmp(opt, "-n")) {
            a.maxFrames = atoi(val);
        } else if (!strcmp(opt, "-t")) {
            if (!strcmp(val, "h264")) {
                a.type = MPP_VIDEO_CodingAVC;
            } else if (!strcmp(val, "h265")) {
                a.type = MPP_VIDEO_CodingHEVC;
            } else {
                printf("-t 只支持 h264 / h265\n");
                return false;
            }
        } else {
            printf("未知参数 %s\n", opt);
            return false;
        }
    }
    if (a.maxFrames < 0) {
        printf("-n 不能是负数\n");
        return false;
    }
    return true;
}

static bool write_nv12_frame(FILE* fp, MppFrame frame) {
    puts("write_nv12_frame");
    const RK_U32 width     = mpp_frame_get_width(frame); // 1920
    const RK_U32 height    = mpp_frame_get_height(frame); // 1080
    const RK_U32 horStride = mpp_frame_get_hor_stride(frame); // 1920
    const RK_U32 verStride = mpp_frame_get_ver_stride(frame); // 1088
    MppBuffer buffer       = mpp_frame_get_buffer(frame);
    if (!buffer) {
        return false;
    }
    // 便于进行指针运算
    const auto* base = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
    printf("    case 1: base address : %p\n", base);
    for (RK_U32 row = 0; row < height; row++) { // Y：1080 行
        if (fwrite(base + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    printf("    case 2: base address : %p\n", base);
    const uint8_t* uv = base + horStride * verStride; // UV 从第 1088 行开始
    for (RK_U32 row = 0; row < height / 2; row++) { // UV：540 行
        if (fwrite(uv + row * horStride, 1, width, fp) != width) {
            return false;
        }
    }
    return true;
}

MPP_RET initDecoderConfig(MppApi* mpp_api, MppCtx mpp_ctx, MppDecCfg dec_cfg) {
    MPP_RET ret = mpp_api->control(mpp_ctx, MPP_DEC_GET_CFG, dec_cfg);
    if (ret != MPP_OK) {
        return ret;
    }
    ret = mpp_dec_cfg_set_u32(dec_cfg, "base:split_parse", 1);
    if (ret != MPP_OK) {
        return ret;
    }
    ret = mpp_api->control(mpp_ctx, MPP_DEC_SET_CFG, dec_cfg);
    if (ret != MPP_OK) {
        return ret;
    }
    return MPP_OK;
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
int main(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        printf("usage: %s -i in.h264 [-o out.nv12] [-t h264|h265] [-n 帧数]\n", argv[0]);
        return -1;
    }
    const size_t chunkSize = 64 * 2014;
    std::vector<uint8_t> chunk(chunkSize);

    MppCtx decoderCtx         = nullptr;
    MppApi* decoderApi        = nullptr;
    MppDecCfg decoderCfg      = nullptr;
    MppBufferGroup frameGroup = nullptr;
    MppPacket inputPacket     = nullptr;
    MppFrame outFrame         = nullptr;
    FILE* streamInputFile     = nullptr;
    FILE* yuvOutputFile       = nullptr;
    bool inputEos             = false;
    bool outputEos            = false;
    bool reachedLimit         = false;
    size_t readSize           = 0;
    RK_S32 decodedFrameCount  = 0;
    RK_S32 errorFrameCount    = 0;
    RK_S32 infoChangeCount    = 0;
    long putRetryCount        = 0;
    std::chrono::steady_clock::time_point startTime;
    double seconds = 0;
    int exitCode   = -1;
    //======M1:init decoder
    CHECK(mpp_create(&decoderCtx, &decoderApi));
    CHECK(mpp_init(decoderCtx, MPP_CTX_DEC, a.type));
    CHECK(mpp_dec_cfg_init(&decoderCfg));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_GET_CFG, decoderCfg));
    CHECK(mpp_dec_cfg_set_s32(decoderCfg, "base:split_parse", 1));
    CHECK(decoderApi->control(decoderCtx, MPP_DEC_SET_CFG, decoderCfg));
    printf("|            decoder ready (%s)\n", a.type == MPP_VIDEO_CodingAVC ? "h264" : "h265");
    streamInputFile = fopen(a.streamInputPath, "rb");
    if (!streamInputFile) {
        printf("open %s failed\n", a.streamInputPath);
        goto CLEANUP;
    }

    if (a.yuvOutputPath) {
        yuvOutputFile = fopen(a.yuvOutputPath, "wb");
        if (!yuvOutputFile) {
            printf("open %s failed\n", a.yuvOutputPath);
            goto CLEANUP;
        }
    }

    //======M2: send packet ,get frame
    startTime = std::chrono::steady_clock::now();
    while (!outputEos && !reachedLimit) {

        // step 1: 读一块码流，
        if (!inputPacket && !inputEos) {
            readSize = fread(chunk.data(), 1, chunkSize, streamInputFile);
            // 读不满，就是文件读完了；
            inputEos = readSize < chunkSize;

            CHECK(mpp_packet_init(&inputPacket, chunk.data(), readSize));

            if (inputEos) {
                mpp_packet_set_eos(inputPacket);
                printf("              input end, send EOS\n");
            }
        }


        // step 2: put the
        if (inputPacket) {
            MPP_RET ret = decoderApi->decode_put_packet(decoderCtx, inputPacket);
            if (ret == MPP_OK) {
                mpp_packet_deinit(&inputPacket);
            } else if (ret == MPP_ERR_BUFFER_FULL) {
                putRetryCount++;
            } else {
                printf("    [FAIL] decode_put_packet ret=%d\n", ret);
                goto CLEANUP;
            }
        }

        // step 3: get frame from decoder by a loop
        while (true) {
            CHECK(decoderApi->decode_get_frame(decoderCtx, &outFrame));
            if (!outFrame) {
                break;
            }
            if (mpp_frame_get_info_change(outFrame)) {
                RK_U32 width            = mpp_frame_get_width(outFrame);
                RK_U32 height           = mpp_frame_get_height(outFrame);
                RK_U32 verStride        = mpp_frame_get_ver_stride(outFrame);
                RK_U32 horStride        = mpp_frame_get_hor_stride(outFrame);
                const size_t bufferSize = mpp_frame_get_buf_size(outFrame); // 该帧需要的缓冲区大小（字节）
                infoChangeCount++;
                printf("|            info change: %ux%u stride %ux%u buf_size %zu\n", width, height, horStride,
                    verStride, bufferSize);
                if (!frameGroup) {
                    CHECK(mpp_buffer_group_get_internal(&frameGroup, MPP_BUFFER_TYPE_DRM));
                } else {
                    CHECK(mpp_buffer_group_clear(frameGroup));
                }
                CHECK(mpp_buffer_group_limit_config(frameGroup, bufferSize, 24));
                decoderApi->control(decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, frameGroup);
                decoderApi->control(decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
            } else {
                RK_U32 errInfo = mpp_frame_get_errinfo(outFrame);
                RK_U32 discard = mpp_frame_get_discard(outFrame);
                if (errInfo || discard) {
                    errorFrameCount++;
                    printf("              frame %-4d err %x discard %x, skip\n", decodedFrameCount, errInfo, discard);
                } else if (mpp_frame_get_buffer(outFrame)) {
                    if (yuvOutputFile && !write_nv12_frame(yuvOutputFile, outFrame)) {
                        perror("    fwrite in error");
                        goto CLEANUP;
                    }
                    decodedFrameCount++;
                    if (a.maxFrames > 0 && decodedFrameCount >= a.maxFrames) {
                        reachedLimit = true;
                    }
                }
            }

            outputEos = mpp_frame_get_eos(outFrame);
            mpp_frame_deinit(&outFrame);
            if (outputEos || reachedLimit) {
                break;
            }
        }
        // step 4:
        if (inputPacket || (inputEos && !outputEos && !reachedLimit)) {
            usleep(1000); // sleep 1ms;
        }
    }

    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    printf("\n===== summary =====\n");
    printf("    input     : %s\n", a.streamInputPath);
    printf("    output    : %s\n", a.yuvOutputPath ? a.yuvOutputPath : "(不写文件)");
    printf("    frames    : %d (error/discard: %d, info change: %d)\n", decodedFrameCount, errorFrameCount,
        infoChangeCount);
    printf("    time      : %.2f s, %.1f fps\n", seconds, seconds > 0 ? decodedFrameCount / seconds : 0.0);
    printf("    put retry : %ld 次（队列满）\n", putRetryCount);
    printf("\n===============================\n");
    exitCode = 0;

CLEANUP:
    if (outFrame) {
        mpp_frame_deinit(&outFrame);
    }

    if (inputPacket) {
        mpp_packet_deinit(&inputPacket);
    }
    if (decoderCfg) {
        mpp_dec_cfg_deinit(decoderCfg);
    }
    if (decoderCtx) {
        mpp_destroy(decoderCtx);
    }
    if (frameGroup) {
        mpp_buffer_group_put(frameGroup);
    }
    if (streamInputFile) {
        fclose(streamInputFile);
    }
    if (yuvOutputFile) {
        fclose(yuvOutputFile);
    }
    return exitCode;
}
