#include <cstdio>
#include <iostream>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
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
    return 0;

CLEANUP:
    if (decCfg)
        mpp_dec_cfg_deinit(decCfg); // 先释放 cfg
    if (mpp_ctx)
        mpp_destroy(mpp_ctx); // 再销毁解码器
    return -1;
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
