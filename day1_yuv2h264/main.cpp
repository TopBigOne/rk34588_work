/*
 * day1_yuv2h264：用 RK3588 的 MPP 硬件编码器，把 NV12 原始图像文件编码成 H.264 / H.265 裸流
 *
 * 整个程序只做 6 件事（和 readme.md 第 2 节对应）：
 *   ① 开一台编码机        mpp_create → mpp_init
 *   ② 告诉它要什么效果    mpp_enc_cfg_init → GET_CFG → CFG_SET × N → SET_CFG
 *   ③ 写文件头            GET_HDR_SYNC → fwrite（SPS/PPS）
 *   ④ 准备硬件内存        mpp_buffer_group_get_internal → mpp_buffer_get
 *   ⑤ 循环每一帧          读 NV12 → sync_end → MppFrame → put_frame → get_packet → fwrite
 *   ⑥ 收拾干净            CLEANUP 里倒着释放
 *
 * 用法：./day1_yuv2h264 -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]
 *                       [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]
 * 板子上运行要先 LD_LIBRARY_PATH=/userdata/mpp_build/lib（出厂的 MPP 库太旧，见 guide/01 第 4 节）
 *
 * 相关文档：guide/06（本文件用到的 MPP 函数）、guide/07（MPP 数据类型）、
 *          nv12中yuv分布效果图和读取方式.md、MppPacket和MppFrame的用法和区别.md
 */
#include <chrono> // std::chrono：统计编码用时
#include <cstdint> // uint8_t
#include <cstdio> // fopen / fread / fwrite / printf
#include <cstdlib> // atoi
#include <cstring> // strcmp
#include <rockchip/rk_mpi.h> // MPP 的总头文件：会间接包含 mpp_buffer.h、mpp_frame.h、mpp_packet.h、
// mpp_meta.h、rk_venc_cfg.h、rk_mpi_cmd.h，用到的 MPP 类型和函数都在里面

// 向上对齐到 a 的整数倍（a 必须是 2 的幂）。例：ALIGN(1080, 16) = 1088
// MPP 自己的 MPP_ALIGN 在内部头文件 osal/inc/mpp_common.h 里，不对外提供，所以自己定义一个
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

// 调用一个返回 MPP_RET 的函数，不等于 MPP_OK 就打印出错的那一句和返回值，然后跳到 CLEANUP 统一释放。
// MPP_RET：MPP_OK = 0 表示成功；负数表示失败，比如 MPP_NOK = -1、MPP_ERR_NULL_PTR = -3、MPP_ERR_VALUE = -6
// 注意 C++ 的 goto 规则：goto 不能跳过带初始化的变量声明，所以 main() 里的变量都集中声明在第一个 CHECK 之前
#define CHECK(expr)                                                            \
    do {                                                                       \
        MPP_RET _r = (expr);                                                   \
        if (_r != MPP_OK) {                                                    \
            printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int) _r, __LINE__); \
            goto CLEANUP;                                                      \
        }                                                                      \
    } while (0)

// 设置一个编码参数。key 是字符串，拼错时 mpp_enc_cfg_set_s32 返回 MPP_NOK，
// 用 CHECK 包起来就能立刻看到是哪一行出错（依赖外面有一个叫 cfg 的变量）
#define CFG_SET(key, val) CHECK(mpp_enc_cfg_set_s32(cfg, key, val))

// M5: 命令行参数，带默认值（不带参数运行就是 1080p30 H.264 CBR 4Mbps）
struct Args {
    const char* inPath  = "/userdata/av/in_1080p_60f.nv12";
    const char* outPath = "/userdata/av/out.h264";
    int width           = 1920;
    int height          = 1080;
    // MppCodingType：编码格式。MPP_VIDEO_CodingAVC = H.264，MPP_VIDEO_CodingHEVC = H.265
    MppCodingType type = MPP_VIDEO_CodingAVC;
    // MppEncRcMode：码率控制模式。CBR 恒定码率 / VBR 可变码率 / AVBR 自适应可变码率
    MppEncRcMode rcMode = MPP_ENC_RC_MODE_CBR;
    int bps             = 4 * 1000 * 1000; // 目标码率，单位 bit/s
    int fps             = 30;
    int gop             = 0; // 0 表示用默认值 fps × 2（每 2 秒一个 I 帧）
};

static void usage(const char* prog) {
    printf("usage: %s -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]\n"
           "          [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]\n",
        prog);
}

/**
 * 解析命令行参数。每个参数都是 "-名字 值" 成对出现
 * 不用 getopt，因为 -rc、-bps 是多个字母，getopt 只认单字母
 * @param argc  main 的 argc
 * @param argv  main 的 argv
 * @param a     解析结果写到这里（引用，函数会改它）
 * @return      参数都合法返回 true；有错返回 false（调用方打印用法后退出）
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
            a.width = atoi(val); // atoi 遇到 "abc" 返回 0，交给下面的合法性检查拦住
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

    // 合法性检查：在申请任何 MPP 资源之前就把非法参数拦下来
    // （比如 -fps 0 传给 MPP 会让程序直接崩溃）
    if (a.width <= 0 || a.height <= 0 || a.width % 2 || a.height % 2) {
        printf("-w / -h 必须是正的偶数（NV12 的 UV 按 2x2 采样），现在是 %dx%d\n", a.width, a.height);
        return false;
    }
    if (a.fps <= 0 || a.bps <= 0 || a.gop < 0) {
        printf("-fps、-bps 必须大于 0，-g 不能是负数\n");
        return false;
    }
    if (a.gop == 0) {
        a.gop = a.fps * 2;
    }
    return true;
}

/**
 * 按 stride 逐行读一帧 NV12，写进 MPP 的硬件缓冲区（照抄 MPP utils/utils.c 里 read_image 的 NV12 分支）
 *
 * 文件里是紧密排列的（没有填充）；硬件缓冲区里每行占 hor_stride 字节、Y 平面占 ver_stride 行（有填充），
 * 所以不能整帧一次 fread，只能一行一行读，每行放到对应的位置。
 * 1920x1080 时：文件里 UV 从第 1080 行开始，缓冲区里 UV 从第 1088 行开始（详见 nv12中yuv分布效果图和读取方式.md）
 *
 * @param fp          输入文件，读完一帧后文件位置正好停在下一帧开头，所以不需要 fseek
 * @param dst         硬件缓冲区的 CPU 地址（mpp_buffer_get_ptr 拿到的）
 * @param width       有效宽度（1920）
 * @param height      有效高度（1080）
 * @param hor_stride  缓冲区里一行占多少字节（1920）
 * @param ver_stride  缓冲区里 Y 平面占多少行（1088）
 * @return  读满一帧返回 true，读到文件尾部返回false
 */
static bool read_nv12_frame(
    FILE* fp, uint8_t* dst, const RK_S32 width, const RK_S32 height, const RK_S32 hor_stride, const RK_S32 ver_stride) {
    // ⚠️️️⚠️️️⚠️️️： 以下是核心，也是我恐惧的地方
    // case 1: 读取Y：height行，每行读取width 字节，写到row*hor_stride 的位置
    //         （每行末尾 hor_stride - width 个字节是填充，不读）
    for (int row = 0; row < height; row++) {
        // C++17 的 if 初始化写法：先读，再判断读到的字节数够不够一行
        if (const size_t readSize = fread(dst + row * hor_stride, 1, width, fp);
            readSize != static_cast<size_t>(width)) {
            return false;
        }
    }
    // case 2 :读取UV，从ver_stride（1088，不是1080） 行开始
    //          只有height/2 行，但是每行还是width字节（U，V交错，UVUVUVUV..）
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
    // 解析参数在第一个 CHECK 之前，失败时直接 return：这时还没申请任何资源，不用走 CLEANUP
    if (!parse_args(argc, argv, a)) {
        usage(argv[0]);
        return -1;
    }

    // stride：硬件编码器要求按 16 对齐。1920 → 1920（本来就是 16 的倍数），1080 → 1088
    RK_S32 hor_stride = ALIGN(a.width, 16);
    RK_S32 ver_stride = ALIGN(a.height, 16);

    // 因为是nv12，所以才：3 / 2（Y 占 1 份，UV 一共占 0.5 份）
    // 硬件缓冲区再按 64 对齐一次，和官方 mpi_enc_test.c 的 test_ctx_init 一样：1920 × 1088 × 3/2 = 3,133,440
    const size_t frameSize    = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2; // 硬件缓冲区大小（带 stride）
    const long fileFrameBytes = (long) a.width * a.height * 3 / 2; // 输入文件里一帧的大小（紧密排列，没有 stride）

    // 码率下限：CBR 要求码率稳定，下限只比目标低 1/16；VBR / AVBR 允许码率随画面变化，下限放宽到目标的 1/16
    const int bpsMin     = (a.rcMode == MPP_ENC_RC_MODE_CBR) ? a.bps * 15 / 16 : a.bps / 16;
    const char* typeName = (a.type == MPP_VIDEO_CodingAVC) ? "h264" : "h265";


    // ----变量（全部声明在第一个 CHECK 之前，满足 goto 规则）
    // 下面这些 MPP 类型，除了 MppApi 是结构体，其余都是 typedef void*（不透明句柄），所以不写 *
    // 详见 guide/07_MPP核心数据类型.md
    MppCtx ctx    = nullptr; // 一个编码器实例（句柄）。所有 mpi->xxx(ctx, ...) 都要把它传回去
    MppApi* mpi   = nullptr; // 操作编码器的函数表：control / encode_put_frame / encode_get_packet ...
    MppEncCfg cfg = nullptr; // 编码参数集合（一张"订单"），用 CFG_SET 按字符串 key 填
    // MppPollType：get_packet 拿不到结果时怎么办。
    // MPP_POLL_BLOCK = 一直等到有结果；MPP_POLL_NON_BLOCK = 立刻返回；正数 = 最多等多少毫秒
    MppPollType timeout = MPP_POLL_BLOCK;
    // vps/sps/pps on each IDR frame
    // MppEncHeaderMode：EACH_IDR = 每个 IDR 帧前面都自动带一份 SPS/PPS，播放器从中间开始播也能解码
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE* fpIn                   = nullptr; // 输入文件（NV12）
    FILE* fpOut                  = nullptr; // 输出文件（H.264 / H.265 裸流）
    MppBufferGroup bufGrp        = nullptr; // DRM 内存池：硬件能访问的内存都从这里申请
    MppBuffer frmBuf             = nullptr; // 装一帧原始图像（NV12），整个循环反复使用这一块
    MppFrame frame               = nullptr; // 描述一帧图像（width, height, stride, format, buffer）
    MppBuffer pktBuf             = nullptr; // 装 SPS/PPS 的缓冲区
    MppPacket packet             = nullptr; // 码流包：描述一段压缩后的码流（pos + length）
    uint8_t* dst                 = nullptr; // frmBuf 的 CPU 地址
    size_t len                   = 0;
    bool frmEos                  = false; // M4: 输入读完了（我们告诉编码器："没有图像了"）
    bool pktEos                  = false; // M4: 输出取完了（编码器告诉我们："码流全给你了"）
    RK_S32 frameCount            = 0; // 编出了多少帧
    RK_S32 iCount                = 0; // I帧数量
    size_t streamSize            = 0; // 码流总字节数（含开头的 SPS/PPS，和输出文件大小一致）
    long fileSize                = 0; // 输入文件大小，用来检查 -w / -h 对不对
    std::chrono::steady_clock::time_point tStart;
    double elapsed = 0; // 编码用时
    double playSec = 0; // 视频时长
    int ret_code   = -1; // 只有全部成功才在最后改成 0；中途 goto CLEANUP 时保持 -1

    // ==================== ① 开一台编码机 ====================
    // 1. create and init：创建实例，拿到 ctx（句柄）和 mpi（函数表）
    // mpp_create(MppCtx* ctx, MppApi** mpi)：两个参数都是"输出参数"，所以传 &ctx、&mpi，函数把结果写回来
    CHECK(mpp_create(&ctx, &mpi));
    // get_packet 阻塞等结果
    // mpi->control(ctx, 命令, 参数)：类似 ioctl，第 2 个参数（MpiCmd）决定干什么，
    // 第 3 个参数（MppParam = void*）的类型由命令决定。这条命令要传 MppPollType 的地址。
    // 按官方示例的顺序，要在 mpp_init 之前设置
    CHECK(mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout));
    // mpp_init：把实例初始化成编码器（MPP_CTX_ENC），并决定编 H.264 还是 H.265。
    // ⚠️ 格式在这一步就定死了，后面再设 codec:type 也改不过来（M5 踩过的坑）
    CHECK(mpp_init(ctx, MPP_CTX_ENC, a.type));

    // ==================== ② 告诉它要什么效果 ====================
    // 2. 先拿默认配置，再改
    // mpp_enc_cfg_init：创建一个空的参数对象；MPP_ENC_GET_CFG：把编码器当前的默认参数读进来
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx, MPP_ENC_GET_CFG, cfg));
    // 输入图像的描述，（必须和内存的排布一致）
    // prep: 开头的参数描述"送进来的图像长什么样"，必须和后面 MppFrame 上设置的 5 个值一致
    CFG_SET("prep:width", a.width);
    CFG_SET("prep:height", a.height);
    CFG_SET("prep:hor_stride", hor_stride);
    CFG_SET("prep:ver_stride", ver_stride);
    CFG_SET("prep:format", MPP_FMT_YUV420SP); // nv12（MPP_FMT_YUV420SP = NV12，SP = Semi-Planar）
    // 码率控制
    // rc: 开头的参数是码率控制（Rate Control）
    CFG_SET("rc:mode", a.rcMode);

    // 帧率：输入、输出都是 -fps 指定的值（flex = 0 表示固定帧率）
    // 帧率用分数表示：num / denom，30 / 1 = 30fps
    CFG_SET("rc:fps_in_flex", 0);
    CFG_SET("rc:fps_in_num", a.fps);
    CFG_SET("rc:fps_in_denom", 1);

    CFG_SET("rc:fps_out_flex", 0);
    CFG_SET("rc:fps_out_num", a.fps);
    CFG_SET("rc:fps_out_denom", 1);
    // 码率上下限：CBR 为目标的 ±1/16；VBR / AVBR 下限放宽到 1/16（bpsMin 在前面算好）
    CFG_SET("rc:bps_target", a.bps);
    CFG_SET("rc:bps_max", a.bps * 17 / 16);
    CFG_SET("rc:bps_min", bpsMin);

    // GOP：每隔多少帧插一个 I 帧。I 帧能独立解码，P 帧要参考前面的帧
    CFG_SET("rc:gop", a.gop);
    CFG_SET("codec:type", a.type);
    // h264 专属参数（h264: 开头的 key 只有 H.264 认识，所以放在 if 里；H.265 用编码器的默认值）
    if (a.type == MPP_VIDEO_CodingAVC) {
        CFG_SET("h264:profile", 100); // High Profile（66 = Baseline，77 = Main，100 = High）
        // level：MPP 只会按分辨率自动调高 level，不看帧率（h264e_sps.c 139～159 行），
        // 1080p 会停在 4.0。帧率超过 30 时要自己设：1080p60 → 4.2，720p60 → 3.2
        if (a.fps > 30) {
            CFG_SET("h264:level", a.height > 720 ? 42 : 32);
        }
        CFG_SET("h264:cabac_en", 1); // 开启cabac ,profile 是Main以上才能使用（CABAC 比 CAVLC 压缩率更高）
        CFG_SET("h264:cabac_idc", 0); // CABAC 初始化表编号，取值 0～2
    }

    // 3. 参数真正生效
    // MPP_ENC_SET_CFG：把 cfg 里的参数交给编码器。不调这一句，前面的 CFG_SET 全部白设
    CHECK(mpi->control(ctx, MPP_ENC_SET_CFG, cfg));
    // 每个IDR帧前，都带SPS/PPS（这条命令要传 MppEncHeaderMode 的地址）
    CHECK(mpi->control(ctx, MPP_ENC_SET_HEADER_MODE, &header_mode));
    printf("|            encoder ready: %dx%d stride %dx%d\n", a.width, a.height, hor_stride, ver_stride);

    // ==================== ③ 写文件头 + ④ 准备硬件内存 ====================
    // M2-3: open the out file
    // "wb"：文件不存在就新建，存在就清空；b = 二进制模式。所在目录必须存在
    fpOut = fopen(a.outPath, "wb");
    if (!fpOut) {
        printf("open %s failed\n", a.outPath);
        goto CLEANUP;
    }
    // M2-4: request the memory which can access the hardware(申请内存)
    // the hardware can direct access the memory ,cpu invoke read() and write() operation via cache are very fast ,
    // but it need invoke sync_end() while operation is in end.
    // 创建内存池：
    //   MPP_BUFFER_TYPE_DRM        通过 DRM 分配，硬件编码器能通过 DMA 直接读写（malloc 的内存硬件访问不了）
    //   MPP_BUFFER_FLAGS_CACHABLE  CPU 读写走缓存（快），代价是 CPU 写完要 mpp_buffer_sync_end 把缓存刷下去
    // &bufGrp：输出参数，函数把新建的内存池写回来
    CHECK(mpp_buffer_group_get_internal(&bufGrp, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    // 从内存池里各拿一块 frameSize 大小的内存（&frmBuf / &pktBuf 也是输出参数）
    CHECK(mpp_buffer_get(bufGrp, &frmBuf, frameSize)); // M3:输入图像，不能用malloc
    CHECK(mpp_buffer_get(bufGrp, &pktBuf, frameSize));
    // M2-5: get SPS/PPS(获取sps，pps)
    // 用 pktBuf 包一个 MppPacket，当成"空容器"交给编码器，让它把 SPS/PPS 写进去
    CHECK(mpp_packet_init_with_buffer(&packet, pktBuf));
    // ⚠️ 一定要清零：init_with_buffer 会把 length 设成整个 buffer 的大小（mpp_packet.cpp 94 行），
    // 不清零编码器会以为容器已经装满了
    mpp_packet_set_length(packet, 0);
    // MPP_ENC_GET_HDR_SYNC：编码器把 SPS/PPS（H.265 还有 VPS）写进 packet。
    // 解码器必须先拿到它们才能解码，所以写在文件最开头
    CHECK(mpi->control(ctx, MPP_ENC_GET_HDR_SYNC, packet));
    // 读码流永远用 pos（有效数据从哪开始）+ length（有多长），不要用 data + size（那是整个容器）
    len = mpp_packet_get_length(packet);
    if (fwrite(mpp_packet_get_pos(packet), 1, len, fpOut) != len) {
        perror("fwrite header"); // 比如板子磁盘满了
        goto CLEANUP;
    }
    streamSize += len;
    printf("|            header : %zu bytes\n", len);
    // 释放 packet（pktBuf 的引用计数 -1，pktBuf 本身还在），deinit 会顺手把 packet 置成 NULL
    mpp_packet_deinit(&packet);

    // M3 单独编 1 帧的代码已经删掉：M4 的循环会从第 0 帧开始编完所有帧
    // M3-3 open the input file（输出文件、内存、SPS/PPS 在 M2 里已经准备好了）
    fpIn = fopen(a.inPath, "rb");
    if (!fpIn) {
        printf("open %s failed\n", a.inPath);
        goto CLEANUP;
    }
    // NV12 文件里没有记录宽高，-w / -h 写错了程序也会照常跑完、结果却是错的。
    // 检查一下文件大小是不是"一帧大小"的整数倍，不是就提醒
    // （局限：恰好能整除时查不出来，比如 1280x720 去读 1080p 文件）
    fseek(fpIn, 0, SEEK_END);
    fileSize = ftell(fpIn);
    rewind(fpIn); // 回到文件开头，后面从第 0 帧开始读
    if (fileSize % fileFrameBytes != 0) {
        printf("warning: 输入文件 %ld 字节，不是一帧 %ld 字节（%dx%d NV12）的整数倍，-w / -h 写对了吗？\n", fileSize,
            fileFrameBytes, a.width, a.height);
    }

    // M3-6 硬件缓冲区的 CPU 地址，循环里每一帧都往这里写
    // mpp_buffer_get_ptr 返回 void*，转成 uint8_t* 才能按字节加偏移
    dst = static_cast<uint8_t*>(mpp_buffer_get_ptr(frmBuf));

    // ==================== ⑤ 循环每一帧 ====================
    // 结束条件是 pktEos（编码器说码流全给完了），不是 frmEos（我们读完了）：
    // 我们读完之后，编码器手里可能还有没吐出来的码流，要等它主动说"结束"才能退出
    tStart = std::chrono::steady_clock::now();
    while (!pktEos) {
        // 5.1 读一帧到硬件缓冲区。读完以后就不再读，后面每次循环只送 EOS
        if (!frmEos) {
            // sync_begin / sync_end 包住 CPU 的写操作：
            // 缓冲区是 CACHABLE 的，CPU 写的数据可能还停在缓存里，sync_end 把它刷到内存，硬件才看得到。
            // 不调 sync_end：硬件读到旧数据，画面花屏、残影
            mpp_buffer_sync_begin(frmBuf);
            frmEos = !read_nv12_frame(fpIn, dst, a.width, a.height, hor_stride, ver_stride);
            mpp_buffer_sync_end(frmBuf);
            if (frmEos) {
                printf("              input end, send EOS\n");
            }
        }
        // M4-6-2: 包装成MppFrame
        // MppFrame 是贴在 frmBuf 上的"标签"：告诉编码器这块内存里的图像多宽多高、stride 多少、什么格式
        CHECK(mpp_frame_init(&frame));
        // 这 5 个值必须和前面的 prep:* 参数一致
        mpp_frame_set_width(frame, a.width);
        mpp_frame_set_height(frame, a.height);
        mpp_frame_set_hor_stride(frame, hor_stride);
        mpp_frame_set_ver_stride(frame, ver_stride);
        mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
        // eos = 1：告诉编码器"这是最后一次了，没有图像了"
        mpp_frame_set_eos(frame, frmEos);
        // 最后一次，送一个空帧，没有图像，只带eos标志
        // set_buffer 会给 frmBuf 的引用计数 +1，所以下面 frame_deinit 时只是 -1，frmBuf 本身不会被释放
        mpp_frame_set_buffer(frame, frmEos ? nullptr : frmBuf);
        // 5.2 送进去：硬件通过 DMA 读 frmBuf，开始压缩。
        // 阻塞模式下，等编码器用完这一帧才返回（mpp.cpp 723～746 行），
        // 所以返回后可以马上释放 frame、往 frmBuf 里写下一帧
        CHECK(mpi->encode_put_frame(ctx, frame));
        mpp_frame_deinit(&frame); // 释放 frame，并把 frame 置成 NULL

        // 5.3 取出来：拿到一个压缩好的 MppPacket（因为设了 MPP_POLL_BLOCK，会一直等到有结果）。
        // 我们没给编码器准备输出 packet，所以这个 packet 的内存是编码器自己分配的，用完由我们 deinit
        CHECK(mpi->encode_get_packet(ctx, &packet));
        if (packet) {
            len = mpp_packet_get_length(packet);
            // 编码器说：这是最后一个，循环就结束
            pktEos = mpp_packet_get_eos(packet);
            // EOS 对应的最后一个包可能是空的（length = 0），不算一帧
            if (len > 0) {
                // 从packe的meta里看看是不是I帧
                // MppMeta 是挂在 packet 上的"附加信息口袋"，按 key 取值。
                // ⚠️ 是 KEY_OUTPUT_INTRA（整数，1 = I 帧），不是 KEY_OUTPUT_FRAME（存的是 frame 对象，用 get_s32
                // 取会失败）
                RK_S32 isIntra = 0;
                if (mpp_packet_has_meta(packet)) {
                    mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &isIntra);
                }
                if (isIntra) {
                    iCount++;
                }
                // H.264 裸流（Annex-B）就是把每个 packet 原样首尾相接写进文件，不需要额外的文件头
                if (fwrite(mpp_packet_get_pos(packet), 1, len, fpOut) != len) {
                    perror("fwrite frame"); // 比如板子磁盘满了
                    goto CLEANUP;
                }
                streamSize += len;
                printf("              frame %-3d size %zu bytes\n", frameCount, len);
                frameCount++;
            }

            // 每个 packet 用完都要释放，否则内存一直涨
            mpp_packet_deinit(&packet);
        }
    }
    // 统计：编码用时包含读文件的时间，不是纯硬件编码时间；
    // 实际码率 = 总字节数 × 8 / 视频时长（帧数 / 帧率）
    elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    playSec = (double) frameCount / a.fps;
    printf("\n===== summary =====\n");
    printf("output    : %s (%s)\n", a.outPath, typeName);
    printf("frames    : %d (I frames: %d)\n", frameCount, iCount);
    printf("time      : %.2f s, %.1f fps\n", elapsed, elapsed > 0 ? frameCount / elapsed : 0.0);
    printf("size      : %zu bytes\n", streamSize);
    printf(
        "bitrate   : %.2f Mbps (target %.2f Mbps)\n", playSec > 0 ? streamSize * 8 / playSec / 1e6 : 0.0, a.bps / 1e6);

    ret_code = 0; // 走到这里说明全部成功

    // ==================== ⑥ 收拾干净 ====================
    // 不管是正常走到这里，还是中途 goto 过来，都在这里统一释放。
    // 每个资源用 if 判断：没申请过（还是 nullptr）就跳过；
    // 顺序大致和申请时相反：先 frame / packet，再编码器，再内存，最后关文件。
    // 一一对应：init ↔ deinit，create ↔ destroy，get ↔ put，fopen ↔ fclose
CLEANUP:
    if (frame) {
        mpp_frame_deinit(&frame);
    }
    if (packet) {
        mpp_packet_deinit(&packet);
    }
    if (cfg) {
        mpp_enc_cfg_deinit(cfg); // 参数对象不属于 ctx，要自己释放
    }
    if (ctx) {
        mpp_destroy(ctx); // 销毁编码器实例。mpi 指向全局函数表，不用释放
    }
    // mpp_buffer_put：引用计数 -1，减到 0 才真正还给内存池。
    // 注意它不会帮你把 frmBuf 置空，所以每个 buffer 只能 put 一次
    if (frmBuf) {
        mpp_buffer_put(frmBuf);
    }
    if (pktBuf) {
        mpp_buffer_put(pktBuf);
    }

    if (bufGrp) {
        mpp_buffer_group_put(bufGrp); // 最后销毁内存池
    }

    if (fpOut) {
        fclose(fpOut); // fclose 会把缓冲区里还没写下去的数据写进文件
    }
    if (fpIn) {
        fclose(fpIn);
    }

    return ret_code;
}
