#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

inline constexpr int64_t kUnfinishedEndTimeMs = -1; // 数据库里 end_time_ms 是 NULL

struct RecordingEntry {
    int64_t recordingId = 0;
    int channelIndex = 0;
    int64_t startTimeMs = 0;
    int64_t endTimeMs = kUnfinishedEndTimeMs;
    std::string filePath;
    int64_t sizeBytes = 0;
};

struct SqliteDatabaseDeleter {
    void operator()(sqlite3 *database) const { sqlite3_close(database); }
};
using SqliteDatabasePtr = std::unique_ptr<sqlite3, SqliteDatabaseDeleter>;

struct SqliteStatementDeleter {
    void operator()(sqlite3_stmt *statement) const { sqlite3_finalize(statement); }
};
using SqliteStatementPtr = std::unique_ptr<sqlite3_stmt, SqliteStatementDeleter>;

// 录像索引：每段一条记录。单线程用（N5 多路时在外面加锁或每路一个连接，N5 决定）
class RecordIndex {
public:
    bool open(const std::string &databaseFilePath); // 不存在就建表、建索引
    void close() { databasePtr.reset(); }
    bool isOpen() const { return databasePtr != nullptr; }

    int64_t insertRecordingStart(int channelIndex, int64_t startTimeMs, const std::string &filePath); // 返回 id；-1 = 失败
    bool finishRecording(int64_t recordingId, int64_t endTimeMs, int64_t sizeBytes);
    // [rangeStartMs, rangeEndMs) 和某段 [start, end) 有重叠就返回它；end 为 NULL（还在录）当作无穷大
    std::vector<RecordingEntry> queryOverlapping(int channelIndex, int64_t rangeStartMs, int64_t rangeEndMs) const;

    // EXPLAIN QUERY PLAN 的输出（每行一句），验收“查询走了索引”用
    std::vector<std::string> explainOverlappingQueryPlan() const;

private:
    SqliteStatementPtr prepareStatement(const char *sqlText) const;

    SqliteDatabasePtr databasePtr;
};
