// 并行线 E 的参考版：递归打印 MP4 的 box 层级，mvhd 打印时长，avcC/hvcC 打印第一个字节，mdat 打印第一帧的前 4 字节。
// 用法：mp4_box_walker 文件.mp4
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static uint32_t readUint32(const uint8_t *data) {
    return (uint32_t{data[0]} << 24) | (uint32_t{data[1]} << 16) | (uint32_t{data[2]} << 8) | data[3];
}

static uint64_t readUint64(const uint8_t *data) {
    return (uint64_t{readUint32(data)} << 32) | readUint32(data + 4);
}

// 这些 box 里面装的还是 box，要往下走
static bool isContainerBox(const std::string &boxType) {
    static const char *const kContainerTypes[] = {"moov", "trak", "mdia", "minf", "stbl", "moof", "traf", "mvex", "edts", "dinf"};
    for (const char *containerType : kContainerTypes) {
        if (boxType == containerType) {
            return true;
        }
    }
    return false;
}

static void printBoxDetail(const std::string &boxType, const uint8_t *payload, uint64_t payloadSizeBytes) {
    if (boxType == "mvhd" && payloadSizeBytes >= 20) {
        const uint8_t version = payload[0];
        const uint32_t timescale = version == 1 ? readUint32(payload + 20) : readUint32(payload + 12);
        const uint64_t duration = version == 1 ? readUint64(payload + 24) : readUint32(payload + 16);
        std::printf("  timescale=%u duration=%llu（%.3f 秒）", timescale, static_cast<unsigned long long>(duration),
                    timescale > 0 ? static_cast<double>(duration) / timescale : 0.0);
    } else if (boxType == "stsd" && payloadSizeBytes >= 8) {
        std::printf("  条目 %u 个", readUint32(payload + 4));
    } else if (boxType == "mdat" && payloadSizeBytes >= 4) {
        // 普通 MP4 里第一帧开头是 4 字节长度（AVCC），不是 00 00 00 01
        std::printf("  前 4 字节 %02x %02x %02x %02x", payload[0], payload[1], payload[2], payload[3]);
    }
}

static void walkBoxes(const uint8_t *data, uint64_t sizeBytes, int depth) {
    uint64_t offset = 0;
    while (offset + 8 <= sizeBytes) {
        uint64_t boxSizeBytes = readUint32(data + offset);
        const std::string boxType(reinterpret_cast<const char *>(data + offset + 4), 4);
        uint64_t headerSizeBytes = 8;
        if (boxSizeBytes == 1 && offset + 16 <= sizeBytes) { // 64 位长度
            boxSizeBytes = readUint64(data + offset + 8);
            headerSizeBytes = 16;
        } else if (boxSizeBytes == 0) { // 一直到文件末尾
            boxSizeBytes = sizeBytes - offset;
        }
        if (boxSizeBytes < headerSizeBytes || offset + boxSizeBytes > sizeBytes) {
            std::printf("%*s%s 长度 %llu 不对（文件可能没写完），停止\n", depth * 2, "", boxType.c_str(),
                        static_cast<unsigned long long>(boxSizeBytes));
            return;
        }
        std::printf("%*s%s %llu", depth * 2, "", boxType.c_str(), static_cast<unsigned long long>(boxSizeBytes));
        const uint8_t *payload = data + offset + headerSizeBytes;
        const uint64_t payloadSizeBytes = boxSizeBytes - headerSizeBytes;
        printBoxDetail(boxType, payload, payloadSizeBytes);
        std::printf("\n");
        if (isContainerBox(boxType)) {
            walkBoxes(payload, payloadSizeBytes, depth + 1);
        } else if (boxType == "stsd" && payloadSizeBytes > 8) {
            // stsd：4 字节版本标志 + 4 字节条目数，后面是 avc1/hev1 条目；条目里跳过 78 字节的视频描述后又是 box（avcC/hvcC）
            const uint8_t *entry = payload + 8;
            const uint64_t entrySizeBytes = readUint32(entry);
            std::printf("%*s%.4s %llu\n", (depth + 1) * 2, "", entry + 4, static_cast<unsigned long long>(entrySizeBytes));
            if (entrySizeBytes > 86 && 8 + entrySizeBytes <= payloadSizeBytes) {
                const uint8_t *configBox = entry + 86;
                std::printf("%*s%.4s %u  configurationVersion=%u\n", (depth + 2) * 2, "", configBox + 4,
                            readUint32(configBox), configBox[8]);
            }
        }
        offset += boxSizeBytes;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法：%s 文件.mp4\n", argv[0]);
        return 1;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "打不开 %s\n", argv[1]);
        return 1;
    }
    const std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    walkBoxes(fileBytes.data(), fileBytes.size(), 0);
    return 0;
}
