#include <cstdlib>
#include <cstring>

#include "app/app_config.h"
#include "app/m1_remux_to_mp4.h"
#include "app/m2_segmented_record.h"
#include "app/m3_record_with_index.h"
#include "app/m4_loop_record.h"
#include "common/log.h"

static const char *const kTag = "Main";

// 用法：n3_recorder m1 [输入] [输出 mp4]
//       n3_recorder m2 | m3 [输入] [录像根目录] [总时长秒] [每段秒]
//       n3_recorder m3query 通道 开始毫秒 结束毫秒 [录像根目录]
//       n3_recorder m4 [输入] [录像根目录] [总时长秒] [每段秒] [配额MB] [实时速度 1/0]
//       n3_recorder m4repair [录像根目录]
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
    if (std::strcmp(milestoneName, "m4") == 0) {
        const char *inputUrl = argc > 2 ? argv[2] : kDefaultInputUrl;
        const char *recordRootPath = argc > 3 ? argv[3] : kRecordRootDir;
        const int totalDurationSeconds = argc > 4 ? std::atoi(argv[4]) : kM4TotalDurationSeconds;
        const int segmentDurationSeconds = argc > 5 ? std::atoi(argv[5]) : kM2SegmentDurationSeconds;
        const int quotaMegabytes = argc > 6 ? std::atoi(argv[6]) : kM4QuotaMegabytes;
        const bool shouldPaceRawFile = argc > 7 ? std::atoi(argv[7]) != 0 : true;
        return runM4LoopRecord(inputUrl, recordRootPath, totalDurationSeconds, segmentDurationSeconds, quotaMegabytes,
                               shouldPaceRawFile);
    }
    if (std::strcmp(milestoneName, "m4repair") == 0) {
        return runM4Repair(argc > 2 ? argv[2] : kRecordRootDir);
    }
    if (std::strcmp(milestoneName, "m3query") == 0 && argc > 4) {
        const char *recordRootPath = argc > 5 ? argv[5] : kRecordRootDir;
        return runM3Query(recordRootPath, std::atoi(argv[2]), std::atoll(argv[3]), std::atoll(argv[4]));
    }
    LOGE(kTag, "用法：%s m1 [输入] [输出 mp4] | m2 | m3 [输入] [录像根目录] [总时长秒] [每段秒] | m3query 通道 开始毫秒 结束毫秒 [录像根目录] | m4 [输入] [根目录] [总秒] [每段秒] [配额MB] [实时1/0] | m4repair [根目录]", argv[0]);
    return 1;
}
