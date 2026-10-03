#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rockchip/rk_mpi.h>
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

// M5: 命令行参数，带默认值
struct Args {
    const char* inPath  = "/userdata/av/in_1080p_60f.nv12";
    const char* outPath = "/userdata/av/out.h264";
    int width           = 1920;
    int height          = 1080;
    MppCodingType type  = MPP_VIDEO_CodingAVC;
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR;
    int bps             = 4 * 1000 * 1000;
    int fps             = 30;
    int gop             = 0;
};

static void usage(const char* prog) {
    printf("usage: %s -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]\n"
           "          [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]\n",
        prog);
}

/**
 * 每个参数都是 "-名字 值" 成对出现
 * 不用 getopt，因为 -rc、-bps 是多个字母，getopt 只认单字母
 * @param argc
 * @param argv
 * @param a
 * @return
 */
static bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i += 2) {
        const char* opt = argv[i];
        const char* val = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!val) {
            printf("参数 %s 缺少值\n", opt);
            return false;
        }

        if (!strcmp(opt, "-i")) {
            a.inPath = val;
        } else if (!strcmp(opt, "-o")) {
            a.outPath = val;
        } else if (!strcmp(opt, "-w")) {
            a.width = atoi(val);
        } else if (!strcmp(opt, "-h")) {
            a.height = atoi(val);
        } else if (!strcmp(opt, "-bps")) {
            a.bps = atoi(val);
        } else if (!strcmp(opt, "-fps")) {
            a.fps = atoi(val);
        } else if (!strcmp(opt, "-g")) {
            a.gop = atoi(val);
        } else if (!strcmp(opt, "-t")) {
            if (!strcmp(val, "h264")) {
                a.type = MPP_VIDEO_CodingAVC;
            } else if (!strcmp(val, "h265")) {
                a.type = MPP_VIDEO_CodingHEVC;
            } else {
                printf("-t 只支持 h264 / h265\n");
                return false;
            }
        } else if (!strcmp(opt, "-rc")) {
            if (!strcmp(val, "cbr")) {
                a.rcMode = MPP_ENC_RC_MODE_CBR;
            } else if (!strcmp(val, "vbr")) {
                a.rcMode = MPP_ENC_RC_MODE_VBR;
            } else if (!strcmp(val, "avbr")) {
                a.rcMode = MPP_ENC_RC_MODE_AVBR;
            } else {
                printf("-rc 只支持 cbr / vbr / avbr\n");
                return false;
            }
        } else {
            printf("未知参数 %s\n", opt);
            return false;
        }
    }
    if (a.gop == 0) {
        a.gop = a.fps * 2;
    }
    return true;
}

/**
 *按 stride 逐行读一帧NV12
 * @param fp
 * @param dst
 * @param width
 * @param height
 * @param hor_stride
 * @param ver_stride
 * @return  读满一帧返回 true，读到文件尾部返回false
 */
static bool read_nv12_frame(
    FILE* fp, uint8_t* dst, const RK_S32 width, const RK_S32 height, const RK_S32 hor_stride, const RK_S32 ver_stride) {
    // ⚠️️️⚠️️️⚠️️️： 以下是核心，也是我恐惧的地方
    // case 1: 读取Y：height行，每行读取width 字节，写到row*hor_stride 的位置
    for (int row = 0; row < height; row++) {
        if (const size_t readSize = fread(dst + row * hor_stride, 1, width, fp);
            readSize != static_cast<size_t>(width)) {
            return false;
        }
    }
    // case 2 :读取UV，从ver_stride（1088，不是1080） 行开始
    //          只有height/2 但是每行哈市width字节（U，V交错，UVUVUVUV..）
    uint8_t* dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (const size_t readSize = fread(dst_uv + row * hor_stride, 1, width, fp);
            readSize != static_cast<size_t>(width)) {
            return false;
        }
    }
    return true;
}


int main(int argc, char** argv) {
    Args a;
    if (!parse_args(argc, argv, a)) {
        usage(argv[0]);
        return -1;
    }

    RK_S32 hor_stride = ALIGN(a.width, 16);
    RK_S32 ver_stride = ALIGN(a.height, 16);

    // 因为是nv12，所以才：3 / 2
    const size_t frameSize = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2; // M2

    const int bpsMin     = (a.rcMode == MPP_ENC_RC_MODE_CBR) ? a.bps * 15 / 16 : a.bps / 16;
    const char* typeName = (a.type == MPP_VIDEO_CodingAVC) ? "h264" : "h265";


    // ----变量
    MppCtx ctx          = nullptr;
    MppApi* mpi         = nullptr;
    MppEncCfg cfg       = nullptr;
    MppPollType timeout = MPP_POLL_BLOCK;
    // vps/sps/pps on each IDR frame
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE* fpIn                   = nullptr; // M3 input file
    FILE* fpOut                  = nullptr; // M2:输出文件
    MppBufferGroup bufGrp        = nullptr; // M2:DRM  内存池
    MppBuffer frmBuf             = nullptr; // M2: 装一帧原始的图像
    MppFrame frame               = nullptr; // M3: 描述一帧图像（width,height,stride ,format ,buffer）
    MppBuffer pktBuf             = nullptr; // M2: 装码流的缓冲区
    MppPacket packet             = nullptr; // M2: 装码包
    uint8_t* dst                 = nullptr; // M3 frmBuf 的cpu地址
    size_t len                   = 0;
    bool frmEos                  = false; // M4: 输入读完了（我们告诉编码器："没有图像了"）
    bool pktEos                  = false; // M4: 输出取完了（编码器告诉我们："码流全给你了"）
    RK_S32 frameCount            = 0; // M4:
    RK_S32 iCount                = 0; // I帧数量
    size_t streamSize            = 0; // M4: 码流总字节数
    std::chrono::steady_clock::time_point tStart;
    double elapsed = 0; // 编码用时
    double playSec = 0; // 视频时长
    int ret_code   = -1;

    // 1. create and init：创建实例，拿到 ctx（句柄）和 mpi（函数表）
    CHECK(mpp_create(&ctx, &mpi));
    // get_packet 阻塞等结果
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, a.type));
    // 2. 先拿默认配置，再改
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));
    // 输入图像的描述，（必须和内存的排布一致）
    mpp_enc_cfg_set_s32(cfg, "prep:width", a.width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", a.height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP); // nv12
    // 码率控制
    mpp_enc_cfg_set_s32(cfg, "rc:mode", a.rcMode);

    // 帧率：输入 30/1,输出 30/1 (flex = 0 ，表示固定帧率)
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", a.fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", 1);

    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", a.fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    // 码率控制的上下 限窄（±1/16）
    CHECK(mpp_enc_cfg_set_s32(cfg, "rc:bps_target", a.bps));
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", a.bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", bpsMin);

    // GOP
    mpp_enc_cfg_set_s32(cfg, "rc:gop", a.gop);
    mpp_enc_cfg_set_s32(cfg, "codec:type", a.type);
    // h264 专属参数
    if (a.type == MPP_VIDEO_CodingAVC) {
        mpp_enc_cfg_set_s32(cfg, "h264:profile", 100); // High Profile
        mpp_enc_cfg_set_s32(cfg, "h264:level", 40); // Level 4.0 ,够1080p@30fps
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1); // 开启cabac ,profile 是Main以上才能使用
        mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0); // CABAC 初始化表编号，取值 0～2
    }

    // 3. 参数真正生效
    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    // 每个IDR帧前，都带SPS/PPS
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("|            encoder ready: %dx%d stride %dx%d\n", a.width, a.height, hor_stride, ver_stride);
    // M2-3: open the out file
    fpOut = fopen(a.outPath, "wb");
    if (!fpOut) {
        printf("open %s failed\n", a.outPath);
        goto CLEANUP;
    }
    // M2-4: request the memory which can access the hardware(申请内存)
    // the hardware can direct access the memory ,cpu invoke read() and write() operation via cache are very fast ,
    // but it need invoke sync_end() while operation is in end.
    CHECK(mpp_buffer_group_get_internal(&bufGrp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(bufGrp, &frmBuf, frameSize)); // M3:输入图像，不能用malloc
    CHECK(mpp_buffer_get(bufGrp, &pktBuf, frameSize));
    // M2-5: get SPS/PPS(获取sps，pps)
    CHECK(mpp_packet_init_with_buffer(&packet, pktBuf));
    mpp_packet_set_length(packet, 0);
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet));
    fwrite(mpp_packet_get_pos(packet), 1, mpp_packet_get_length(packet), fpOut);
    printf("|            header : %zu bytes\n", mpp_packet_get_length(packet));
    mpp_packet_deinit(&packet);

    // M3 单独编 1 帧的代码已经删掉：M4 的循环会从第 0 帧开始编完所有帧
    // M3-3 open the input file（输出文件、内存、SPS/PPS 在 M2 里已经准备好了）
    fpIn = fopen(a.inPath, "rb");
    if (!fpIn) {
        printf("open %s failed\n", a.inPath);
        goto CLEANUP;
    }

    // M3-6 硬件缓冲区的 CPU 地址，循环里每一帧都往这里写
    dst = static_cast<uint8_t*>(mpp_buffer_get_ptr(frmBuf));

    tStart = std::chrono::steady_clock::now();
    while (!pktEos) {
        if (!frmEos) {
            mpp_buffer_sync_begin(frmBuf);
            frmEos = !read_nv12_frame(fpIn, dst, a.width, a.height, hor_stride, ver_stride);
            mpp_buffer_sync_end(frmBuf);
            if (frmEos) {
                printf("              input end, send EOS\n");
            }
        }
        // M4-6-2: 包装成MppFrame
        CHECK(mpp_frame_init(&frame));
        mpp_frame_set_width(frame, a.width);
        mpp_frame_set_height(frame, a.height);
        mpp_frame_set_hor_stride(frame, hor_stride);
        mpp_frame_set_ver_stride(frame, ver_stride);
        mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
        mpp_frame_set_eos(frame, frmEos);
        // 最后一次，送一个空帧，没有图像，只带eos标志
        mpp_frame_set_buffer(frame, frmEos ? nullptr : frmBuf);
        CHECK(mpi->encode_put_frame(ctx, frame));
        mpp_frame_deinit(&frame);

        CHECK(mpi->encode_get_packet(ctx, &packet));
        if (packet) {
            len = mpp_packet_get_length(packet);
            // 编码器说：这是最后一个，循环就结束
            pktEos = mpp_packet_get_eos(packet);
            if (len > 0) {
                // 从packe的meta里看看是不是I帧
                RK_S32 isIntra = 0;
                if (mpp_packet_has_meta(packet)) {
                    mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &isIntra);
                }
                if (isIntra) {
                    iCount++;
                }
                fwrite(mpp_packet_get_pos(packet), 1, len, fpOut);
                streamSize += len;
                printf("              frame %-3d size %zu bytes\n", frameCount, len);
                frameCount++;
            }

            mpp_packet_deinit(&packet);
        }
    }
    elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    playSec = (double) frameCount / a.fps;
    printf("\n===== summary =====\n");
    printf("output    : %s (%s)\n", a.outPath, typeName);
    printf("frames    : %d (I frames: %d)\n", frameCount, iCount);
    printf("time      : %.2f s, %.1f fps\n", elapsed, elapsed > 0 ? frameCount / elapsed : 0.0);
    printf("size      : %zu bytes\n", streamSize);
    printf(
        "bitrate   : %.2f Mbps (target %.2f Mbps)\n", playSec > 0 ? streamSize * 8 / playSec / 1e6 : 0.0, a.bps / 1e6);

    ret_code = 0;

CLEANUP:
    if (frame) {
        mpp_frame_deinit(&frame);
    }
    if (packet) {
        mpp_packet_deinit(&packet);
    }
    if (cfg) {
        mpp_enc_cfg_deinit(cfg);
    }
    if (ctx) {
        mpp_destroy(ctx);
    }
    if (frmBuf) {
        mpp_buffer_put(frmBuf);
    }
    if (pktBuf) {
        mpp_buffer_put(pktBuf);
    }

    if (bufGrp) {
        mpp_buffer_group_put(bufGrp);
    }

    if (fpOut) {
        fclose(fpOut);
    }
    if (fpIn) {
        fclose(fpIn);
    }

    return ret_code;
}
