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


int main() {
    // ---------------------M1：编码器初始化------------------------start
    RK_S32 width = 1920;
    RK_S32 height = 1080;
    RK_S32 hor_stride = ALIGN(width, 16);
    RK_S32 ver_stride = ALIGN(height, 16);
    RK_S32 fps = 30;
    RK_S32 bps = 4 * 1000 * 1000;
    RK_S32 gop = fps * 2;

    MppCtx ctx = nullptr;
    MppApi *mpi = nullptr;
    MppEncCfg cfg = nullptr;
    MppPollType timeout = MPP_POLL_BLOCK;
    // vps/sps/pps on each IDR frame
    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
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
    ret_code = 0;


CLEANUP:
    if (cfg) {
        mpp_enc_cfg_deinit(cfg);
    }
    if (ctx) {
        mpp_destroy(ctx);
    }

    return ret_code;
}
