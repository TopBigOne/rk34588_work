#include <cstring> // strcmp

#include "app/m1_decode_channels.h"
#include "app/m2_compose_mosaic.h"
#include "app/m3_encode_mosaic.h"
#include "common/log.h"

static const char *const kTag = "Main";

/**
 * @brief 程序入口：按第一个参数选择跑哪个里程碑
 * @param[in] argc  参数个数（包括程序名）
 * @param[in] argv  参数；argv[1] 是里程碑名字："m1"、"m2"、"m3"，不写默认 "m3"
 * @return 0 = 成功；1 = 失败或参数不认识
 * @note main 只做分发，不放业务逻辑
 */
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m3";
    if (strcmp(milestoneName, "m1") == 0) {
        return runM1DecodeChannels();
    }
    if (strcmp(milestoneName, "m2") == 0) {
        return runM2ComposeMosaic();
    }
    if (strcmp(milestoneName, "m3") == 0) {
        return runM3EncodeMosaic();
    }
    LOGE(kTag, "用法：%s m1 | m2 | m3", argv[0]);
    return 1;
}
