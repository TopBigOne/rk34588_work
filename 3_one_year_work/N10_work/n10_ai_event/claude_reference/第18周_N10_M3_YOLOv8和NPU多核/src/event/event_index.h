#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

// 在 N3 的录像索引库里加一张 events 表（同一个 .db 文件，N3 的 recordings 表不动；SQLite 允许多个连接）。
// 时间用墙钟毫秒，和 N3 recordings 表一致，回放时能按时间把事件和录像对上。
struct EventRecord {
    int64_t eventId = 0;  // 插入时不用填
    int channelIndex = 0;
    int64_t startTimeMs = 0;
    int64_t endTimeMs = 0;
    int maxPersonCount = 0;
    std::string filePath;
};

class EventIndex {
public:
    bool open(const std::string &requestedDatabasePath);  // 不存在就建表、建索引
    bool isOpen() const { return databasePtr != nullptr; }
    int64_t insertEvent(const EventRecord &eventRecord);  // 返回 id；-1 = 失败
    std::vector<EventRecord> listEvents(int channelIndex) const;  // 按 start_time_ms 升序

private:
    struct SqliteDatabaseCloser {
        void operator()(sqlite3 *database) const { sqlite3_close(database); }
    };
    std::unique_ptr<sqlite3, SqliteDatabaseCloser> databasePtr;
};
