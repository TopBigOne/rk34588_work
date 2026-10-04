/*
 * 【解析参数】的实现，说明见 args.h
 */
#include "app/args.h"

#include <cstdio> // printf
#include <cstdlib> // atoi
#include <cstring> // strcmp

void usage(const char* prog) {
    printf("usage: %s -i in.nv12 -o out.h264 [-w 1920] [-h 1080] [-t h264|h265]\n"
           "          [-rc cbr|vbr|avbr] [-bps 4000000] [-fps 30] [-g 60]\n",
        prog);
}

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; i += 2) {
        const char* opt = argv[i];
        const char* val = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!val) {
            printf("参数 %s 缺少值\n", opt);
            return false;
        }

        if (!strcmp(opt, "-i")) {
            a.nv12InputPath = val;
        } else if (!strcmp(opt, "-o")) {
            a.streamOutputPath = val;
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
 * 因为这个函数是按值返回：返回类型是 EncoderConfig，不是引用也不是指针.
 * return cfg; 时，调用方会拿到 cfg 的一份副本（或者编译器直接把 cfg 构造在调用方那边，见下面）。
 * 函数结束后局部变量 cfg 确实销毁了，但调用方手里的对象和它是两个独立的对象，没有任何关系。
 * @param a
 * @return
 */
EncoderConfig to_encoder_config(const Args& a) {
    EncoderConfig cfg;
    cfg.width  = a.width;
    cfg.height = a.height;
    cfg.type   = a.type;
    cfg.rcMode = a.rcMode;
    cfg.bps    = a.bps;
    cfg.fps    = a.fps;
    cfg.gop    = a.gop;
    return cfg;
}
