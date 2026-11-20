#include <cstdlib>
#include <cstring>

#include "app/app_config.h"
#include "app/m1_remux_to_mp4.h"
#include "app/m2_segmented_record.h"
#include "common/log.h"

static const char *const kTag = "Main";

// 用法：n3_recorder m1 [输入] [输出 mp4]
//       n3_recorder m2 [输入] [录像根目录] [总时长秒] [每段秒]
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m1";
    if (std::strcmp(milestoneName, "m1") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *outputPath = argc > 3 ? argv[3] : kM1OutputPath;
        return runM1RemuxToMp4(inputUrl, outputPath);
    }
    if (std::strcmp(milestoneName, "m2") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *recordRootPath = argc > 3 ? argv[3] : kRecordRootDir;
        const int totalDurationSeconds = argc > 4 ? std::atoi(argv[4]) : kM2TotalDurationSeconds;
        const int segmentDurationSeconds = argc > 5 ? std::atoi(argv[5]) : kM2SegmentDurationSeconds;
        return runM2SegmentedRecord(inputUrl, recordRootPath, totalDurationSeconds, segmentDurationSeconds);
    }
    LOGE(kTag, "用法：%s m1 [输入] [输出 mp4] | m2 [输入] [录像根目录] [总时长秒] [每段秒]", argv[0]);
    return 1;
}
