#include <cstring>

#include "app/m0_detect_image.h"
#include "common/log.h"

static const char *const kTag = "Main";

int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m0";
    if (strcmp(milestoneName, "m0") == 0) {
        // m0 [模型路径] [图片路径]
        return runM0DetectImage(argc > 2 ? argv[2] : nullptr, argc > 3 ? argv[3] : nullptr);
    }
    LOGE(kTag, "用法：%s m0 [模型.rknn] [图片.jpg]", argv[0]);
    return 1;
}
