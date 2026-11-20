#include "storage/record_path_builder.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <system_error>

#include "common/log.h"

static const char *const kTag = "RecordPath";

int64_t getWallClockMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string buildSegmentFilePath(const std::string &recordRootPath, int channelIndex, int64_t segmentStartTimeMs) {
    const time_t startTimeSeconds = static_cast<time_t>(segmentStartTimeMs / 1000);
    tm localTime{};
    localtime_r(&startTimeSeconds, &localTime); // 线程安全版的 localtime：结果写进我们给的 tm
    char dateText[16] = {0};
    char timeText[16] = {0};
    strftime(dateText, sizeof(dateText), "%Y-%m-%d", &localTime);
    strftime(timeText, sizeof(timeText), "%H-%M-%S", &localTime);

    const std::filesystem::path directoryPath =
            std::filesystem::path(recordRootPath) / ("ch" + std::to_string(channelIndex)) / dateText;
    std::error_code errorCode;
    std::filesystem::create_directories(directoryPath, errorCode);
    if (errorCode) {
        LOGE(kTag, "建目录 %s 失败：%s", directoryPath.c_str(), errorCode.message().c_str());
        return "";
    }
    std::filesystem::path filePath = directoryPath / (std::string(timeText) + ".mp4");
    for (int suffixIndex = 1; std::filesystem::exists(filePath); ++suffixIndex) {
        filePath = directoryPath / (std::string(timeText) + "_" + std::to_string(suffixIndex) + ".mp4");
    }
    return filePath.string();
}
