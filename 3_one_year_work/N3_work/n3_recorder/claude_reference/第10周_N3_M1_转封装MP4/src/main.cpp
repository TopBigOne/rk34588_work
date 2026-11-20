#include <cstring>

#include "app/app_config.h"
#include "app/m1_remux_to_mp4.h"
#include "common/log.h"

static const char *const kTag = "Main";

// 用法：n3_recorder m1 [输入文件或 rtsp://URL] [输出 mp4]
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m1";
    if (std::strcmp(milestoneName, "m1") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *outputPath = argc > 3 ? argv[3] : kM1OutputPath;
        return runM1RemuxToMp4(inputUrl, outputPath);
    }
    LOGE(kTag, "用法：%s m1 [输入文件或 rtsp://URL] [输出 mp4]", argv[0]);
    return 1;
}
