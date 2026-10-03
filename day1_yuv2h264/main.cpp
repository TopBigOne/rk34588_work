#include <cstdio>
#include <rockchip/rk_mpi.h>
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CHECK(expr) do{\
    MPP_RET _r = (expr);\
    if(_r!=MPP_OK){  \
      printf("[FAIL] %s ret=%d (line %d)\n", #expr, (int)_r, __LINE__); \
      goto CLEANUP;\
    }\
}while (0)

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
static bool read_nv12_frame(FILE *fp, uint8_t *dst,
                            const RK_S32 width,
                            const RK_S32 height,
                            const RK_S32 hor_stride,
                            const RK_S32 ver_stride) {
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
    uint8_t *dst_uv = dst + hor_stride * ver_stride;
    for (int row = 0; row < height / 2; row++) {
        if (const size_t readSize = fread(dst_uv + row * hor_stride, 1, width, fp);
            readSize != static_cast<size_t>(width)) {
            return false;
        }
    }
    return true;
}


int main() {
    // ---------------------M1：编码器初始化------------------------start
    RK_S32 width = 1920;
    RK_S32 height = 1080;
    RK_S32 hor_stride = ALIGN(width, 16);
    RK_S32 ver_stride = ALIGN(height, 16);
    RK_S32 fps = 30;
    RK_S32 bps = 4 * 1000 * 1000;
    RK_S32 gop = fps * 2;
    const char *outPath = "/userdata/av/out.h264"; // M2
    const char *inPath = "/userdata/av/in_1080p_60f.nv12";

    // 因为是nv12，所以才：3 / 2
    const size_t frameSize = ALIGN(hor_stride, 64) * ALIGN(ver_stride, 64) * 3 / 2; // M2

    // ----变量
    MppCtx ctx = nullptr;
    MppApi *mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType timeout = MPP_POLL_BLOCK;
    // vps/sps/pps on each IDR frame
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    FILE *fpIn = nullptr; // M3 input file
    FILE *fpOut = nullptr; // M2:输出文件
    MppBufferGroup bufGrp = nullptr; // M2:DRM  内存池
    MppBuffer frmBuf = nullptr; // M2: 装一帧原始的图像
    MppFrame frame = nullptr; // M3: 描述一帧图像（width,height,stride ,format ,buffer）
    MppBuffer pktBuf = nullptr; // M2: 装码流的缓冲区
    MppPacket packet = nullptr; // M2: 装码包
    uint8_t *dst = nullptr; // M3 frmBuf 的cpu地址
    size_t len = 0;

    int ret_code = -1;

    // 1. create and init：创建实例，拿到 ctx（句柄）和 mpi（函数表）
    CHECK(mpp_create(&ctx,&mpi));
    // get_packet 阻塞等结果
    CHECK(mpi->control(ctx,MPP_SET_OUTPUT_TIMEOUT,&timeout));
    CHECK(mpp_init(ctx,MPP_CTX_ENC,MPP_VIDEO_CodingAVC));
    // 2. 先拿默认配置，再改
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(mpi->control(ctx,MPP_ENC_GET_CFG,cfg));
    // 输入图像的描述，（必须和内存的排布一致）
    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", hor_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", ver_stride);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP); // nv12
    // 码率控制:CBR
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);

    // 帧率：输入 30/1,输出 30/1 (flex = 0 ，表示固定帧率)
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denom", 1);

    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denom", 1);
    // 码率控制的上下 限窄（±1/16）
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", bps * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", bps * 15 / 16);

    // GOP
    mpp_enc_cfg_set_s32(cfg, "rc:gop", gop);

    // h264 专属参数
    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(cfg, "h264:profile", 100); // High Profile
    mpp_enc_cfg_set_s32(cfg, "h264:level", 40); // Level 4.0 ,够1080p@30fps
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 1); // 开启cabac ,profile 是Main以上才能使用
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_idc", 0); // CABAC 初始化表编号，取值 0～2

    // 3. 参数真正生效
    CHECK(mpi->control(ctx,MPP_ENC_SET_CFG,cfg));
    // 每个IDR帧前，都带SPS/PPS
    CHECK(mpi->control(ctx,MPP_ENC_SET_HEADER_MODE,&header_mode));

    printf("|M1 result:\n");
    printf("|            encoder ready: %dx%d stride %dx%d\n", width, height, hor_stride, ver_stride);
    printf("| --------------------------------------------------------------------------------\n");

    // ---------------------M1：编码器初始化------------------------end

    // ---------------------M2：获取pps,sps------------------------start
    // M2-3: open the out file
    fpOut = fopen(outPath, "wb");
    if (!fpOut) {
        printf("open %s failed\n", outPath);
        goto CLEANUP;
    }
    // M2-4: request the memory which can access the hardware(申请内存)
    // the hardware can direct access the memory ,cpu invoke read() and write() operation via cache are very fast ,
    // but it need invoke sync_end() while operation is in end.
    CHECK(mpp_buffer_group_get_internal(&bufGrp,MPP_BUFFER_TYPE_DRM|MPP_BUFFER_FLAGS_CACHABLE));
    CHECK(mpp_buffer_get(bufGrp,&frmBuf,frameSize)); // M3:输入图像，不能用malloc
    CHECK(mpp_buffer_get(bufGrp,&pktBuf,frameSize));
    // M2-5: get SPS/PPS(获取sps，pps)
    CHECK(mpp_packet_init_with_buffer(&packet,pktBuf));
    mpp_packet_set_length(packet, 0);
    CHECK(mpi->control(ctx,MPP_ENC_GET_HDR_SYNC,packet));
    fwrite(mpp_packet_get_pos(packet),
           1, mpp_packet_get_length(packet),
           fpOut);
    printf("|M2 result:\n");
    printf("|            header : %zu bytes\n", mpp_packet_get_length(packet));
    printf("| --------------------------------------------------------------------------------\n");
    mpp_packet_deinit(&packet);

    // ---------------------M2：获取pps,sps------------------------end


    // ---------------------M3：编码 1 帧------------------------start
    // M3-3 open the input file（输出文件、内存、SPS/PPS 在 M2 里已经准备好了）
    fpIn = fopen(inPath, "rb");
    if (!fpIn) {
        printf("open %s failed\n", inPath);
        goto CLEANUP;
    }

    // M3-6 读一帧到硬件缓冲区
    dst = static_cast<uint8_t *>(mpp_buffer_get_ptr(frmBuf)); // 硬件缓冲区的cpu地址
    mpp_buffer_sync_begin(frmBuf); // cpu开始写
    if (!read_nv12_frame(fpIn, dst, width, height, hor_stride, ver_stride)) {
        printf("read frame failed, 输入文件太小？\n");
        goto CLEANUP;
    }
    mpp_buffer_sync_end(frmBuf); // CPU 写完，刷缓存，硬件才能看到

    // M3-7 包装成MppFrame
    CHECK(mpp_frame_init(&frame));
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, hor_stride);
    mpp_frame_set_ver_stride(frame, ver_stride);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_eos(frame, 0); // 不是最后一帧
    mpp_frame_set_buffer(frame, frmBuf);
    // M3-8 送进去，取出来
    CHECK(mpi->encode_put_frame(ctx,frame)); // 阻塞，等硬件读完
    mpp_frame_deinit(&frame); // frame 用完就释放，frmBuf 还在
    CHECK(mpi->encode_get_packet(ctx,&packet)); // 阻塞，等编码完成
    if (packet) {
        len = mpp_packet_get_length(packet);
        fwrite(mpp_packet_get_pos(packet), 1, len, fpOut);
        printf("frame 0 size %zu bytes\n", len);
        mpp_packet_deinit(&packet);
    }

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
