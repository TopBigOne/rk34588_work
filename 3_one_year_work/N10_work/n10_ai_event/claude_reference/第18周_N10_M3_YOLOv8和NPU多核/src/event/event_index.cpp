#include "event/event_index.h"

#include "common/log.h"

static const char *const kTag = "EventIndex";

static const char *const kCreateTableSql = R"SQL(
CREATE TABLE IF NOT EXISTS events (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    channel_index    INTEGER NOT NULL,
    start_time_ms    INTEGER NOT NULL,
    end_time_ms      INTEGER NOT NULL,
    max_person_count INTEGER NOT NULL,
    file_path        TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_events_channel_start ON events(channel_index, start_time_ms);
)SQL";

struct SqliteStatementFinalizer {
    void operator()(sqlite3_stmt *statement) const { sqlite3_finalize(statement); }
};
using SqliteStatementPtr = std::unique_ptr<sqlite3_stmt, SqliteStatementFinalizer>;

bool EventIndex::open(const std::string &requestedDatabasePath) {
    sqlite3 *rawDatabase = nullptr;
    int ret = sqlite3_open(requestedDatabasePath.c_str(), &rawDatabase);
    databasePtr.reset(rawDatabase);  // 失败时 sqlite3_open 也可能给出句柄，同样要 close
    if (ret != SQLITE_OK) {
        LOGE(kTag, "打开 %s 失败：%s", requestedDatabasePath.c_str(), sqlite3_errstr(ret));
        databasePtr.reset();
        return false;
    }
    // 和 N3 的写入进程同时开同一个库时，对方正在写就等最多 2 秒，而不是立刻报 SQLITE_BUSY
    sqlite3_busy_timeout(rawDatabase, 2000);
    char *errorMessage = nullptr;
    ret = sqlite3_exec(rawDatabase, kCreateTableSql, nullptr, nullptr, &errorMessage);
    if (ret != SQLITE_OK) {
        LOGE(kTag, "建表失败：%s", errorMessage != nullptr ? errorMessage : "?");
        sqlite3_free(errorMessage);
        databasePtr.reset();
        return false;
    }
    return true;
}

int64_t EventIndex::insertEvent(const EventRecord &eventRecord) {
    if (!databasePtr) {
        return -1;
    }
    sqlite3_stmt *rawStatement = nullptr;
    const char *insertSql = "INSERT INTO events(channel_index, start_time_ms, end_time_ms, max_person_count, file_path) "
                            "VALUES(?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(databasePtr.get(), insertSql, -1, &rawStatement, nullptr) != SQLITE_OK) {
        LOGE(kTag, "prepare 失败：%s", sqlite3_errmsg(databasePtr.get()));
        return -1;
    }
    SqliteStatementPtr statementPtr(rawStatement);
    sqlite3_bind_int(rawStatement, 1, eventRecord.channelIndex);
    sqlite3_bind_int64(rawStatement, 2, eventRecord.startTimeMs);
    sqlite3_bind_int64(rawStatement, 3, eventRecord.endTimeMs);
    sqlite3_bind_int(rawStatement, 4, eventRecord.maxPersonCount);
    sqlite3_bind_text(rawStatement, 5, eventRecord.filePath.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(rawStatement) != SQLITE_DONE) {
        LOGE(kTag, "插入失败：%s", sqlite3_errmsg(databasePtr.get()));
        return -1;
    }
    return sqlite3_last_insert_rowid(databasePtr.get());
}

std::vector<EventRecord> EventIndex::listEvents(int channelIndex) const {
    std::vector<EventRecord> eventRecords;
    if (!databasePtr) {
        return eventRecords;
    }
    sqlite3_stmt *rawStatement = nullptr;
    const char *selectSql = "SELECT id, channel_index, start_time_ms, end_time_ms, max_person_count, file_path "
                            "FROM events WHERE channel_index = ? ORDER BY start_time_ms";
    if (sqlite3_prepare_v2(databasePtr.get(), selectSql, -1, &rawStatement, nullptr) != SQLITE_OK) {
        return eventRecords;
    }
    SqliteStatementPtr statementPtr(rawStatement);
    sqlite3_bind_int(rawStatement, 1, channelIndex);
    while (sqlite3_step(rawStatement) == SQLITE_ROW) {
        EventRecord eventRecord;
        eventRecord.eventId = sqlite3_column_int64(rawStatement, 0);
        eventRecord.channelIndex = sqlite3_column_int(rawStatement, 1);
        eventRecord.startTimeMs = sqlite3_column_int64(rawStatement, 2);
        eventRecord.endTimeMs = sqlite3_column_int64(rawStatement, 3);
        eventRecord.maxPersonCount = sqlite3_column_int(rawStatement, 4);
        eventRecord.filePath = reinterpret_cast<const char *>(sqlite3_column_text(rawStatement, 5));
        eventRecords.push_back(eventRecord);
    }
    return eventRecords;
}
