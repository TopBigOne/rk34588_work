#include <cstdlib>
#include <cstring>

#include "app/app_config.h"
#include "app/m1_remux_to_mp4.h"
#include "app/m2_segmented_record.h"
#include "app/m3_record_with_index.h"
#include "common/log.h"

static const char *const kTag = "Main";

// 用法：n3_recorder m1 [输入] [输出 mp4]
//       n3_recorder m2 | m3 [输入] [录像根目录] [总时长秒] [每段秒]
//       n3_recorder m3query 通道 开始毫秒 结束毫秒 [录像根目录]
int main(int argc, char **argv) {
    const char *milestoneName = argc > 1 ? argv[1] : "m1";
    if (std::strcmp(milestoneName, "m1") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *outputPath = argc > 3 ? argv[3] : kM1OutputPath;
        return runM1RemuxToMp4(inputUrl, outputPath);
    }
    if (std::strcmp(milestoneName, "m2") == 0 || std::strcmp(milestoneName, "m3") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *recordRootPath = argc > 3 ? argv[3] : kRecordRootDir;
        const int totalDurationSeconds = argc > 4 ? std::atoi(argv[4]) : kM2TotalDurationSeconds;
        const int segmentDurationSeconds = argc > 5 ? std::atoi(argv[5]) : kM2SegmentDurationSeconds;
        if (std::strcmp(milestoneName, "m2") == 0) {
            return runM2SegmentedRecord(inputUrl, recordRootPath, totalDurationSeconds, segmentDurationSeconds);
        }
        return runM3RecordWithIndex(inputUrl, recordRootPath, totalDurationSeconds, segmentDurationSeconds);
    }
    if (std::strcmp(milestoneName, "m3query") == 0 && argc > 4) {
        const char *recordRootPath = argc > 5 ? argv[5] : kRecordRootDir;
        return runM3Query(recordRootPath, std::atoi(argv[2]), std::atoll(argv[3]), std::atoll(argv[4]));
    }
    LOGE(kTag, "用法：%s m1 [输入] [输出 mp4] | m2 | m3 [输入] [录像根目录] [总时长秒] [每段秒] | m3query 通道 开始毫秒 结束毫秒 [录像根目录]", argv[0]);
    return 1;
}
