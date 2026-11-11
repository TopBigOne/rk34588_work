#include <cstring> // strcmp
#include <iostream>

#include "app/milestones.h"
#include "common/log.h"

static const char *const kTag = "Main";

/**
 * @brief 程序入口：按第一个参数选择跑哪一项
 * @param[in] argc  参数个数（包括程序名）
 * @param[in] argv  参数；argv[1] 是要跑的那一项的名字，目前只有 "decode"
 * @return 0 = 成功；1 = 失败或参数不认识
 * @note main 只做分发，不放业务逻辑
 */
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "decode";
    if (strcmp(milestoneName, "decode") == 0) {
        return runM1DecodeOnlyCheck();
    }
    LOGE(kTag, "用法：%s decode", argv[0]);
    return 1;
}
