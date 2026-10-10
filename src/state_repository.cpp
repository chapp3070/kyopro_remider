#include "atcoder_reminder/state_repository.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <filesystem>
#include <sstream>

namespace reminder {
namespace {

std::string sqliteError(sqlite3* db, const std::string& prefix) {
    return prefix + ": " + (db != nullptr ? sqlite3_errmsg(db) : "database is null");
}

const char* statusText(NotificationStatus status) {
    switch (status) {
    case NotificationStatus::None: return "none";
    case NotificationStatus::Pending: return "pending";
    case NotificationStatus::Sent: return "sent";
    }
    return "none";
}

NotificationStatus parseStatus(const unsigned char* value, NotificationStatus fallback) {
    if (value == nullptr) return fallback;
    const std::string text(reinterpret_cast<const char*>(value));
    if (text == "pending") return NotificationStatus::Pending;
    if (text == "sent") return NotificationStatus::Sent;
    if (text == "none") return NotificationStatus::None;
    return fallback;
}

const char* kindText(NotificationKind kind) {
    return kind == NotificationKind::Normal ? "normal" : "change";
}

std::optional<NotificationKind> parseKind(const unsigned char* value) {
    if (value == nullptr) return std::nullopt;
    const std::string text(reinterpret_cast<const char*>(value));
    if (text == "normal") return NotificationKind::Normal;
    if (text == "change") return NotificationKind::ScheduleChanged;
    return std::nullopt;
}

std::optional<UnixSeconds> optionalInteger(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    return static_cast<UnixSeconds>(sqlite3_column_int64(statement, column));
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

} // namespace

SqliteStateRepository::SqliteStateRepository(std::string path) : path_(std::move(path)) {}

SqliteStateRepository::~SqliteStateRepository() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteStateRepository::initialize(std::string& error) {
    try {
        const std::filesystem::path databasePath(path_);
        if (databasePath.has_parent_path()) {
            std::filesystem::create_directories(databasePath.parent_path());
        }
    } catch (const std::exception& exception) {
        error = std::string("could not create state directory: ") + exception.what();
        return false;
    }

    if (sqlite3_open_v2(path_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
        != SQLITE_OK) {
        error = sqliteError(db_, "could not open SQLite database");
        return false;
    }
    if (!exec("PRAGMA journal_mode=WAL;", error)
        || !exec("PRAGMA synchronous=FULL;", error)
        || !exec("PRAGMA busy_timeout=5000;", error)) {
        return false;
    }
    return exec(
        "CREATE TABLE IF NOT EXISTS current_contest ("
        "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
        "contest_id TEXT NOT NULL, contest_number INTEGER NOT NULL,"
        "start_time INTEGER NOT NULL, end_time INTEGER NOT NULL, reminder_time INTEGER NOT NULL,"
        "normal_status TEXT NOT NULL CHECK (normal_status IN ('pending','sent')),"
        "normal_sent_at_start INTEGER,"
        "change_status TEXT NOT NULL CHECK (change_status IN ('none','pending','sent')),"
        "change_sent_at_start INTEGER,"
        "pending_kind TEXT CHECK (pending_kind IN ('normal','change')),"
        "pending_key TEXT, last_fetched_at INTEGER, updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS notification_history ("
        "message_key TEXT PRIMARY KEY, contest_id TEXT NOT NULL,"
        "kind TEXT NOT NULL CHECK (kind IN ('normal','change')), start_time INTEGER NOT NULL,"
        "status TEXT NOT NULL CHECK (status IN ('pending','sent','skipped')),"
        "discord_message_id TEXT, updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS weekly_absence_checks ("
        "check_date TEXT PRIMARY KEY, completed_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS next_contest_notices ("
        "message_key TEXT PRIMARY KEY, previous_contest_id TEXT NOT NULL,"
        "previous_start_time INTEGER NOT NULL, previous_end_time INTEGER NOT NULL,"
        "contest_id TEXT NOT NULL, contest_number INTEGER NOT NULL,"
        "start_time INTEGER NOT NULL, end_time INTEGER NOT NULL,"
        "status TEXT NOT NULL CHECK (status IN ('pending','sent','skipped')),"
        "discord_message_id TEXT, updated_at INTEGER NOT NULL"
        ");",
        error);
}

bool SqliteStateRepository::weeklyCheckCompleted(const std::string& date,
                                                 bool& completed,
                                                 std::string& error) {
    const char* sql = "SELECT 1 FROM weekly_absence_checks WHERE check_date = ?;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare weekly check lookup");
        return false;
    }
    const bool bound = bindText(statement, 1, date);
    const int result = bound ? sqlite3_step(statement) : SQLITE_ERROR;
    if (!bound || (result != SQLITE_ROW && result != SQLITE_DONE)) {
        error = sqliteError(db_, "could not read weekly check state");
        sqlite3_finalize(statement);
        return false;
    }
    completed = result == SQLITE_ROW;
    sqlite3_finalize(statement);
    return true;
}

bool SqliteStateRepository::markWeeklyCheckCompleted(const std::string& date,
                                                    std::string& error) {
    const char* sql =
        "INSERT OR IGNORE INTO weekly_absence_checks (check_date, completed_at) VALUES (?, ?);";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare weekly check save");
        return false;
    }
    const bool okay = bindText(statement, 1, date)
        && sqlite3_bind_int64(statement, 2, currentUnixSeconds()) == SQLITE_OK
        && sqlite3_step(statement) == SQLITE_DONE;
    if (!okay) error = sqliteError(db_, "could not save weekly check state");
    sqlite3_finalize(statement);
    return okay;
}

bool SqliteStateRepository::advanceToNextContest(const CurrentContest& previous,
                                                const CurrentContest& next,
                                                std::string& error) {
    if (previous.contest.endTime > next.updatedAt
        || next.contest.id == previous.contest.id
        || next.contest.startTime <= next.updatedAt
        || next.contest.endTime <= next.contest.startTime) {
        error = "cannot advance before contest end or to an invalid future contest";
        return false;
    }
    if (!exec("BEGIN IMMEDIATE;", error)) return false;
    const std::string key = "next:" + previous.contest.id + ":"
        + std::to_string(previous.contest.startTime);
    const char* sql =
        "INSERT OR IGNORE INTO next_contest_notices "
        "(message_key, previous_contest_id, previous_start_time, previous_end_time, "
        "contest_id, contest_number, start_time, end_time, status, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?);";
    sqlite3_stmt* statement = nullptr;
    bool okay = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) == SQLITE_OK;
    if (okay) {
        okay = bindText(statement, 1, key)
            && bindText(statement, 2, previous.contest.id)
            && sqlite3_bind_int64(statement, 3, previous.contest.startTime) == SQLITE_OK
            && sqlite3_bind_int64(statement, 4, previous.contest.endTime) == SQLITE_OK
            && bindText(statement, 5, next.contest.id)
            && sqlite3_bind_int(statement, 6, next.contest.number) == SQLITE_OK
            && sqlite3_bind_int64(statement, 7, next.contest.startTime) == SQLITE_OK
            && sqlite3_bind_int64(statement, 8, next.contest.endTime) == SQLITE_OK
            && sqlite3_bind_int64(statement, 9, next.updatedAt) == SQLITE_OK
            && sqlite3_step(statement) == SQLITE_DONE;
    }
    const bool inserted = okay && sqlite3_changes(db_) == 1;
    if (statement != nullptr) sqlite3_finalize(statement);
    if (!okay || (inserted && !saveInternal(next, {}, error))) {
        if (error.empty()) error = sqliteError(db_, "could not queue next contest notice");
        exec("ROLLBACK;", error);
        return false;
    }
    if (!exec("COMMIT;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    return true;
}

std::optional<NextContestNotice> SqliteStateRepository::loadPendingNextContestNotice(
    std::string& error) {
    error.clear();
    const char* sql =
        "SELECT message_key, contest_id, contest_number, start_time, end_time "
        "FROM next_contest_notices WHERE status='pending' "
        "ORDER BY previous_end_time, message_key LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare next contest notice load");
        return std::nullopt;
    }
    const int result = sqlite3_step(statement);
    if (result != SQLITE_ROW) {
        if (result != SQLITE_DONE) error = sqliteError(db_, "could not load next contest notice");
        sqlite3_finalize(statement);
        return std::nullopt;
    }
    NextContestNotice notice;
    notice.key = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    notice.contest.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    notice.contest.number = sqlite3_column_int(statement, 2);
    notice.contest.startTime = sqlite3_column_int64(statement, 3);
    notice.contest.endTime = sqlite3_column_int64(statement, 4);
    notice.contest.url = "https://atcoder.jp/contests/" + notice.contest.id;
    sqlite3_finalize(statement);
    return notice;
}

bool SqliteStateRepository::refreshNextContestNotice(const std::string& key,
                                                     const Contest& contest,
                                                     std::string& error) {
    const char* sql =
        "UPDATE next_contest_notices SET contest_number=?, start_time=?, end_time=?, updated_at=? "
        "WHERE message_key=? AND contest_id=? AND status='pending';";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare next contest schedule refresh");
        return false;
    }
    const bool okay = sqlite3_bind_int(statement, 1, contest.number) == SQLITE_OK
        && sqlite3_bind_int64(statement, 2, contest.startTime) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, contest.endTime) == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, currentUnixSeconds()) == SQLITE_OK
        && bindText(statement, 5, key)
        && bindText(statement, 6, contest.id)
        && sqlite3_step(statement) == SQLITE_DONE
        && sqlite3_changes(db_) == 1;
    if (!okay) error = sqliteError(db_, "could not refresh pending next contest schedule");
    sqlite3_finalize(statement);
    return okay;
}

bool SqliteStateRepository::updateNextContestNotice(const std::string& key,
                                                   const std::string& status,
                                                   const std::string& discordMessageId,
                                                   std::string& error) {
    const char* sql =
        "UPDATE next_contest_notices SET status=?, discord_message_id=?, updated_at=? "
        "WHERE message_key=? AND status='pending';";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare next contest notice update");
        return false;
    }
    const bool okay = bindText(statement, 1, status)
        && bindText(statement, 2, discordMessageId)
        && sqlite3_bind_int64(statement, 3, currentUnixSeconds()) == SQLITE_OK
        && bindText(statement, 4, key)
        && sqlite3_step(statement) == SQLITE_DONE;
    if (!okay) error = sqliteError(db_, "could not update next contest notice");
    sqlite3_finalize(statement);
    return okay;
}

bool SqliteStateRepository::markNextContestNoticeSent(const std::string& key,
                                                     const std::string& discordMessageId,
                                                     std::string& error) {
    return updateNextContestNotice(key, "sent", discordMessageId, error);
}

bool SqliteStateRepository::markNextContestNoticeSkipped(const std::string& key,
                                                        std::string& error) {
    return updateNextContestNotice(key, "skipped", {}, error);
}

bool SqliteStateRepository::exec(const char* sql, std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        error = message != nullptr ? message : sqliteError(db_, "SQLite operation failed");
        sqlite3_free(message);
        return false;
    }
    return true;
}

std::optional<CurrentContest> SqliteStateRepository::load(std::string& error) {
    const char* sql =
        "SELECT contest_id, contest_number, start_time, end_time, reminder_time, "
        "normal_status, normal_sent_at_start, change_status, change_sent_at_start, "
        "pending_kind, pending_key, last_fetched_at, updated_at "
        "FROM current_contest WHERE singleton = 1;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare state load");
        return std::nullopt;
    }

    const int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        error = sqliteError(db_, "could not read state");
        sqlite3_finalize(statement);
        return std::nullopt;
    }

    CurrentContest state;
    state.contest.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    state.contest.number = sqlite3_column_int(statement, 1);
    state.contest.startTime = sqlite3_column_int64(statement, 2);
    state.contest.endTime = sqlite3_column_int64(statement, 3);
    state.reminderTime = sqlite3_column_int64(statement, 4);
    state.normalStatus = parseStatus(sqlite3_column_text(statement, 5), NotificationStatus::Pending);
    state.normalSentForStartTime = optionalInteger(statement, 6);
    state.changeStatus = parseStatus(sqlite3_column_text(statement, 7), NotificationStatus::None);
    state.changeSentForStartTime = optionalInteger(statement, 8);
    state.pendingKind = parseKind(sqlite3_column_text(statement, 9));
    const unsigned char* pendingKey = sqlite3_column_text(statement, 10);
    state.pendingMessageKey = pendingKey != nullptr
        ? reinterpret_cast<const char*>(pendingKey) : "";
    state.lastFetchedAt = sqlite3_column_int64(statement, 11);
    state.updatedAt = sqlite3_column_int64(statement, 12);
    state.contest.url = "https://atcoder.jp/contests/" + state.contest.id;
    sqlite3_finalize(statement);
    return state;
}

bool SqliteStateRepository::saveInternal(const CurrentContest& state,
                                         const std::string& /*key*/,
                                         std::string& error) {
    const char* sql =
        "INSERT INTO current_contest (singleton, contest_id, contest_number, start_time, end_time,"
        "reminder_time, normal_status, normal_sent_at_start, change_status, change_sent_at_start,"
        "pending_kind, pending_key, last_fetched_at, updated_at)"
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        "ON CONFLICT(singleton) DO UPDATE SET contest_id=excluded.contest_id,"
        "contest_number=excluded.contest_number, start_time=excluded.start_time,"
        "end_time=excluded.end_time, reminder_time=excluded.reminder_time,"
        "normal_status=excluded.normal_status, normal_sent_at_start=excluded.normal_sent_at_start,"
        "change_status=excluded.change_status, change_sent_at_start=excluded.change_sent_at_start,"
        "pending_kind=excluded.pending_kind, pending_key=excluded.pending_key,"
        "last_fetched_at=excluded.last_fetched_at, updated_at=excluded.updated_at;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare state save");
        return false;
    }

    const std::string pendingKind = state.pendingKind.has_value()
        ? kindText(*state.pendingKind) : "";
    bool okay = bindText(statement, 1, state.contest.id)
        && sqlite3_bind_int(statement, 2, state.contest.number) == SQLITE_OK
        && sqlite3_bind_int64(statement, 3, state.contest.startTime) == SQLITE_OK
        && sqlite3_bind_int64(statement, 4, state.contest.endTime) == SQLITE_OK
        && sqlite3_bind_int64(statement, 5, state.reminderTime) == SQLITE_OK
        && bindText(statement, 6, statusText(state.normalStatus));
    if (okay) {
        okay = state.normalSentForStartTime.has_value()
            ? sqlite3_bind_int64(statement, 7, *state.normalSentForStartTime) == SQLITE_OK
            : sqlite3_bind_null(statement, 7) == SQLITE_OK;
    }
    if (okay) okay = bindText(statement, 8, statusText(state.changeStatus));
    if (okay) {
        okay = state.changeSentForStartTime.has_value()
            ? sqlite3_bind_int64(statement, 9, *state.changeSentForStartTime) == SQLITE_OK
            : sqlite3_bind_null(statement, 9) == SQLITE_OK;
    }
    if (okay) {
        okay = state.pendingKind.has_value()
            ? bindText(statement, 10, pendingKind)
            : sqlite3_bind_null(statement, 10) == SQLITE_OK;
    }
    if (okay) {
        okay = state.pendingKind.has_value()
            ? bindText(statement, 11, state.pendingMessageKey)
            : sqlite3_bind_null(statement, 11) == SQLITE_OK;
    }
    if (okay) okay = sqlite3_bind_int64(statement, 12, state.lastFetchedAt) == SQLITE_OK;
    if (okay) okay = sqlite3_bind_int64(statement, 13, state.updatedAt) == SQLITE_OK;

    if (!okay || sqlite3_step(statement) != SQLITE_DONE) {
        error = sqliteError(db_, "could not save state");
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    return true;
}

bool SqliteStateRepository::save(const CurrentContest& state, std::string& error) {
    if (!exec("BEGIN IMMEDIATE;", error)) return false;
    if (!saveInternal(state, {}, error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    if (!exec("COMMIT;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool SqliteStateRepository::clear(std::string& error) {
    if (!exec("BEGIN IMMEDIATE;", error)) return false;
    if (!exec("DELETE FROM current_contest;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    if (!exec("COMMIT;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool SqliteStateRepository::markPending(NotificationKind kind,
                                        const std::string& key,
                                        const CurrentContest& state,
                                        std::string& error) {
    if (!exec("BEGIN IMMEDIATE;", error)) return false;

    const char* sql =
        "INSERT OR IGNORE INTO notification_history "
        "(message_key, contest_id, kind, start_time, status, updated_at) "
        "VALUES (?, ?, ?, ?, 'pending', ?);";
    sqlite3_stmt* statement = nullptr;
    bool okay = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) == SQLITE_OK;
    if (okay) {
        const UnixSeconds now = state.updatedAt;
        okay = bindText(statement, 1, key)
            && bindText(statement, 2, state.contest.id)
            && bindText(statement, 3, kindText(kind))
            && sqlite3_bind_int64(statement, 4, state.contest.startTime) == SQLITE_OK
            && sqlite3_bind_int64(statement, 5, now) == SQLITE_OK
            && sqlite3_step(statement) == SQLITE_DONE;
    }
    if (statement != nullptr) sqlite3_finalize(statement);
    if (!okay || !saveInternal(state, key, error)) {
        if (error.empty()) error = sqliteError(db_, "could not mark pending");
        exec("ROLLBACK;", error);
        return false;
    }
    if (!exec("COMMIT;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool SqliteStateRepository::updateHistoryStatus(const std::string& key,
                                                const std::string& status,
                                                const std::string& discordMessageId,
                                                std::string& error) {
    const char* sql =
        "UPDATE notification_history SET status = ?, discord_message_id = ?, updated_at = ? "
        "WHERE message_key = ?;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqliteError(db_, "could not prepare notification history update");
        return false;
    }
    const UnixSeconds now = currentUnixSeconds();
    const bool okay = bindText(statement, 1, status)
        && bindText(statement, 2, discordMessageId)
        && sqlite3_bind_int64(statement, 3, now) == SQLITE_OK
        && bindText(statement, 4, key)
        && sqlite3_step(statement) == SQLITE_DONE;
    if (!okay) error = sqliteError(db_, "could not update notification history");
    sqlite3_finalize(statement);
    return okay;
}

bool SqliteStateRepository::markSent(const std::string& key,
                                     const std::string& discordMessageId,
                                     const CurrentContest& state,
                                     std::string& error) {
    if (!exec("BEGIN IMMEDIATE;", error)) return false;
    if (!updateHistoryStatus(key, "sent", discordMessageId, error)
        || !saveInternal(state, key, error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    if (!exec("COMMIT;", error)) {
        exec("ROLLBACK;", error);
        return false;
    }
    return true;
}

} // namespace reminder
