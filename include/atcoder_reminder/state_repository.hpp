#pragma once

#include "atcoder_reminder/ports.hpp"

#include <sqlite3.h>

namespace reminder {

class SqliteStateRepository final : public StateRepository {
public:
    explicit SqliteStateRepository(std::string path);
    ~SqliteStateRepository() override;

    bool initialize(std::string& error) override;
    std::optional<CurrentContest> load(std::string& error) override;
    bool save(const CurrentContest& state, std::string& error) override;
    bool clear(std::string& error) override;
    bool markPending(NotificationKind kind,
                     const std::string& key,
                     const CurrentContest& state,
                     std::string& error) override;
    bool markSent(const std::string& key,
                  const std::string& discordMessageId,
                  const CurrentContest& state,
                  std::string& error) override;
    bool weeklyCheckCompleted(const std::string& date,
                              bool& completed,
                              std::string& error);
    bool markWeeklyCheckCompleted(const std::string& date, std::string& error);
    bool advanceToNextContest(const CurrentContest& previous,
                              const CurrentContest& next,
                              std::string& error);
    std::optional<NextContestNotice> loadPendingNextContestNotice(std::string& error);
    bool refreshNextContestNotice(const std::string& key,
                                  const Contest& contest,
                                  std::string& error);
    bool markNextContestNoticeSent(const std::string& key,
                                   const std::string& discordMessageId,
                                   std::string& error);
    bool markNextContestNoticeSkipped(const std::string& key, std::string& error);

private:
    bool exec(const char* sql, std::string& error);
    bool saveInternal(const CurrentContest& state,
                      const std::string& key,
                      std::string& error);
    bool updateHistoryStatus(const std::string& key,
                             const std::string& status,
                             const std::string& discordMessageId,
                             std::string& error);

    bool updateNextContestNotice(const std::string& key,
                                 const std::string& status,
                                 const std::string& discordMessageId,
                                 std::string& error);

    std::string path_;
    sqlite3* db_{nullptr};
};

} // namespace reminder
