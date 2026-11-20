#include "storage/record_index.h"

#include "common/log.h"

static const char *const kTag = "RecordIndex";

static const char *const kCreateTableSql = R"SQL(
CREATE TABLE IF NOT EXISTS recordings (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    channel_index INTEGER NOT NULL,
    start_time_ms INTEGER NOT NULL,
    end_time_ms   INTEGER,
    file_path     TEXT    NOT NULL UNIQUE,
    size_bytes    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_recordings_channel_start ON recordings(channel_index, start_time_ms);
)SQL";

// [a1,a2) 和 [b1,b2) 重叠 ⇔ a1 < b2 AND b1 < a2。?1 通道，?2 查询开始，?3 查询结束
static const char *const kQueryOverlappingSql =
        "SELECT id, channel_index, start_time_ms, end_time_ms, file_path, size_bytes FROM recordings "
        "WHERE channel_index = ?1 AND start_time_ms < ?3 AND (end_time_ms IS NULL OR end_time_ms > ?2) "
        "ORDER BY start_time_ms";

static RecordingEntry readEntryFromRow(sqlite3_stmt *statement) {
    RecordingEntry entry;
    entry.recordingId = sqlite3_column_int64(statement, 0);
    entry.channelIndex = sqlite3_column_int(statement, 1);
    entry.startTimeMs = sqlite3_column_int64(statement, 2);
    entry.endTimeMs =
            sqlite3_column_type(statement, 3) == SQLITE_NULL ? kUnfinishedEndTimeMs : sqlite3_column_int64(statement, 3);
    const unsigned char *filePathText = sqlite3_column_text(statement, 4);
    entry.filePath = filePathText != nullptr ? reinterpret_cast<const char *>(filePathText) : "";
    entry.sizeBytes = sqlite3_column_int64(statement, 5);
    return entry;
}

bool RecordIndex::open(const std::string &databaseFilePath) {
    sqlite3 *rawDatabase = nullptr;
    // 打开失败时 sqlite3_open 也可能分配了句柄，要 close，所以先交给 unique_ptr 再判断
    const int openRet = sqlite3_open(databaseFilePath.c_str(), &rawDatabase);
    databasePtr.reset(rawDatabase);
    if (openRet != SQLITE_OK) {
        LOGE(kTag, "打开 %s 失败：%s", databaseFilePath.c_str(),
             rawDatabase != nullptr ? sqlite3_errmsg(rawDatabase) : "内存不够");
        databasePtr.reset();
        return false;
    }
    char *errorMessage = nullptr;
    if (sqlite3_exec(databasePtr.get(), kCreateTableSql, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        LOGE(kTag, "建表失败：%s", errorMessage != nullptr ? errorMessage : "?");
        sqlite3_free(errorMessage);
        databasePtr.reset();
        return false;
    }
    return true;
}

SqliteStatementPtr RecordIndex::prepareStatement(const char *sqlText) const {
    sqlite3_stmt *rawStatement = nullptr;
    if (!databasePtr || sqlite3_prepare_v2(databasePtr.get(), sqlText, -1, &rawStatement, nullptr) != SQLITE_OK) {
        LOGE(kTag, "prepare 失败：%s", databasePtr ? sqlite3_errmsg(databasePtr.get()) : "数据库没打开");
        sqlite3_finalize(rawStatement);
        return SqliteStatementPtr();
    }
    return SqliteStatementPtr(rawStatement);
}

int64_t RecordIndex::insertRecordingStart(int channelIndex, int64_t startTimeMs, const std::string &filePath) {
    SqliteStatementPtr statementPtr = prepareStatement(
            "INSERT INTO recordings (channel_index, start_time_ms, end_time_ms, file_path) VALUES (?1, ?2, NULL, ?3)");
    if (!statementPtr) {
        return -1;
    }
    sqlite3_bind_int(statementPtr.get(), 1, channelIndex);
    sqlite3_bind_int64(statementPtr.get(), 2, startTimeMs);
    // SQLITE_TRANSIENT：让 sqlite 自己复制一份字符串，不依赖 filePath 活多久
    sqlite3_bind_text(statementPtr.get(), 3, filePath.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statementPtr.get()) != SQLITE_DONE) {
        LOGE(kTag, "插入 %s 失败：%s", filePath.c_str(), sqlite3_errmsg(databasePtr.get()));
        return -1;
    }
    return sqlite3_last_insert_rowid(databasePtr.get());
}

bool RecordIndex::finishRecording(int64_t recordingId, int64_t endTimeMs, int64_t sizeBytes) {
    SqliteStatementPtr statementPtr =
            prepareStatement("UPDATE recordings SET end_time_ms = ?1, size_bytes = ?2 WHERE id = ?3");
    if (!statementPtr) {
        return false;
    }
    sqlite3_bind_int64(statementPtr.get(), 1, endTimeMs);
    sqlite3_bind_int64(statementPtr.get(), 2, sizeBytes);
    sqlite3_bind_int64(statementPtr.get(), 3, recordingId);
    if (sqlite3_step(statementPtr.get()) != SQLITE_DONE) {
        LOGE(kTag, "补结束时间失败（id %lld）：%s", static_cast<long long>(recordingId), sqlite3_errmsg(databasePtr.get()));
        return false;
    }
    if (sqlite3_changes(databasePtr.get()) != 1) {
        LOGE(kTag, "id %lld 不存在", static_cast<long long>(recordingId));
        return false;
    }
    return true;
}

std::vector<RecordingEntry> RecordIndex::queryOverlapping(int channelIndex, int64_t rangeStartMs,
                                                          int64_t rangeEndMs) const {
    std::vector<RecordingEntry> entries;
    SqliteStatementPtr statementPtr = prepareStatement(kQueryOverlappingSql);
    if (!statementPtr) {
        return entries;
    }
    sqlite3_bind_int(statementPtr.get(), 1, channelIndex);
    sqlite3_bind_int64(statementPtr.get(), 2, rangeStartMs);
    sqlite3_bind_int64(statementPtr.get(), 3, rangeEndMs);
    while (sqlite3_step(statementPtr.get()) == SQLITE_ROW) {
        entries.push_back(readEntryFromRow(statementPtr.get()));
    }
    return entries;
}

std::vector<RecordingEntry> RecordIndex::queryEntries(const char *sqlText) const {
    std::vector<RecordingEntry> entries;
    SqliteStatementPtr statementPtr = prepareStatement(sqlText);
    if (!statementPtr) {
        return entries;
    }
    while (sqlite3_step(statementPtr.get()) == SQLITE_ROW) {
        entries.push_back(readEntryFromRow(statementPtr.get()));
    }
    return entries;
}

bool RecordIndex::findOldestRecording(RecordingEntry &oldestEntry) const {
    const std::vector<RecordingEntry> entries = queryEntries(
            "SELECT id, channel_index, start_time_ms, end_time_ms, file_path, size_bytes FROM recordings "
            "ORDER BY start_time_ms, id LIMIT 1");
    if (entries.empty()) {
        return false;
    }
    oldestEntry = entries.front();
    return true;
}

bool RecordIndex::deleteRecording(int64_t recordingId) {
    SqliteStatementPtr statementPtr = prepareStatement("DELETE FROM recordings WHERE id = ?1");
    if (!statementPtr) {
        return false;
    }
    sqlite3_bind_int64(statementPtr.get(), 1, recordingId);
    if (sqlite3_step(statementPtr.get()) != SQLITE_DONE) {
        LOGE(kTag, "删记录 %lld 失败：%s", static_cast<long long>(recordingId), sqlite3_errmsg(databasePtr.get()));
        return false;
    }
    return true;
}

int64_t RecordIndex::sumRecordingSizeBytes() const {
    // total() 和 sum() 的区别：没有行时 sum() 返回 NULL，total() 返回 0.0；这里用 ifnull(sum(...), 0) 保持整数
    SqliteStatementPtr statementPtr = prepareStatement("SELECT ifnull(sum(size_bytes), 0) FROM recordings");
    if (!statementPtr || sqlite3_step(statementPtr.get()) != SQLITE_ROW) {
        return -1;
    }
    return sqlite3_column_int64(statementPtr.get(), 0);
}

std::vector<RecordingEntry> RecordIndex::listUnfinishedRecordings() const {
    return queryEntries("SELECT id, channel_index, start_time_ms, end_time_ms, file_path, size_bytes FROM recordings "
                        "WHERE end_time_ms IS NULL ORDER BY start_time_ms");
}

std::vector<RecordingEntry> RecordIndex::listAllRecordings() const {
    return queryEntries("SELECT id, channel_index, start_time_ms, end_time_ms, file_path, size_bytes FROM recordings "
                        "ORDER BY start_time_ms");
}

std::vector<std::string> RecordIndex::explainOverlappingQueryPlan() const {
    std::vector<std::string> planLines;
    SqliteStatementPtr statementPtr = prepareStatement((std::string("EXPLAIN QUERY PLAN ") + kQueryOverlappingSql).c_str());
    if (!statementPtr) {
        return planLines;
    }
    // 输出 4 列：id、parent、notused、detail；我们只要 detail
    while (sqlite3_step(statementPtr.get()) == SQLITE_ROW) {
        const unsigned char *detailText = sqlite3_column_text(statementPtr.get(), 3);
        planLines.emplace_back(detailText != nullptr ? reinterpret_cast<const char *>(detailText) : "");
    }
    return planLines;
}
