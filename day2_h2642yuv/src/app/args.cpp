// 【扫一眼】参数解析，原样从面条版搬过来，属于样板代码
#include "app/args.h"

#include <cstdio>  // printf
#include <cstdlib> // atoi
#include <cstring> // strcmp

/**
 * @brief 打印用法
 * @param prog  程序名（argv[0]）
 * @note 【扫一眼】
 */
void usage(const char* prog) {
    printf("usage: %s -i in.h264 [-o out.nv12] [-t h264|h265] [-n 帧数]\n", prog);
}

/**
 * @brief 解析命令行参数，每个参数都是 "-名字 值" 成对出现
 * @param argc  main 的 argc
 * @param argv  main 的 argv
 * @param a     输出参数：解析结果
 * @return 参数都合法返回 true；有错打印原因，返回 false
 * @note 【扫一眼】
 */
bool parse_args(int argc, char** argv, Args& a) {
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
