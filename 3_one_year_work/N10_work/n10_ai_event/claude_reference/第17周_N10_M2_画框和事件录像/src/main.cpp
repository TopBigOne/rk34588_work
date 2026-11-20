#include <cstring>

#include "app/m0_detect_image.h"
#include "app/m1_detect_file.h"
#include "app/m2_event_record.h"
#include <cstdlib>
#include "common/log.h"

static const char *const kTag = "Main";

static const char *getArgOrNull(int argc, char **argv, int argIndex) {
    return argc > argIndex ? argv[argIndex] : nullptr;
}

int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m2";
    if (strcmp(milestoneName, "m0") == 0) {
        // m0 [模型.rknn] [图片.jpg]
        return runM0DetectImage(getArgOrNull(argc, argv, 2), getArgOrNull(argc, argv, 3));
    }
    if (strcmp(milestoneName, "m1") == 0) {
        // m1 [码流.h264] [模型.rknn] [letterbox|stretch]
        const char *resizeMode = getArgOrNull(argc, argv, 4);
        bool isLetterboxEnabled = resizeMode == nullptr || strcmp(resizeMode, "stretch") != 0;
        return runM1DetectFile(getArgOrNull(argc, argv, 2), getArgOrNull(argc, argv, 3), isLetterboxEnabled);
    }
    if (strcmp(milestoneName, "m1cmp") == 0) {
        // m1cmp [模型.rknn] [图片.jpg]
        return runM1ComparePostProcess(getArgOrNull(argc, argv, 2), getArgOrNull(argc, argv, 3));
    }
    if (strcmp(milestoneName, "m2") == 0) {
        // m2 [码流文件或 rtsp://] [模型.rknn] [文件放几遍]
        const char *loopCountText = getArgOrNull(argc, argv, 4);
        return runM2EventRecord(getArgOrNull(argc, argv, 2), getArgOrNull(argc, argv, 3),
                                loopCountText != nullptr ? atoi(loopCountText) : 1);
    }
    LOGE(kTag, "用法：%s m0 [模型] [图片] | m1 [码流] [模型] [letterbox|stretch] | m1cmp [模型] [图片] | "
               "m2 [码流或 rtsp://] [模型] [放几遍]",
         argv[0]);
    return 1;
}
