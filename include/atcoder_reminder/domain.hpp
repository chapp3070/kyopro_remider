#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reminder {

using UnixSeconds = std::int64_t;

struct Contest {
    std::string id;
    int number{};
    UnixSeconds startTime{};
    UnixSeconds endTime{};
    std::string url;
};

enum class NotificationKind {
    Normal,
    ScheduleChanged,
};

enum class NotificationStatus {
    None,
    Pending,
    Sent,
};

struct CurrentContest {
    Contest contest;
    UnixSeconds reminderTime{};
    NotificationStatus normalStatus{NotificationStatus::Pending};
    std::optional<UnixSeconds> normalSentForStartTime;
    NotificationStatus changeStatus{NotificationStatus::None};
    std::optional<UnixSeconds> changeSentForStartTime;
    std::optional<NotificationKind> pendingKind;
    std::string pendingMessageKey;
    UnixSeconds lastFetchedAt{};
    UnixSeconds updatedAt{};
};

struct NextContestNotice {
    std::string key;
    Contest contest;
};

struct ParseResult {
    bool tableFound{false};
    std::vector<Contest> contests;
    std::string error;
    bool hasInvalidAbcRows{false};
    bool hasAbcToday{false};
};

struct HttpResponse {
    long status{0};
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;

    bool transportSucceeded() const { return error.empty(); }
};

enum class SendResult {
    Sent,
    RetryableError,
    ConfigurationError,
};

} // namespace reminder
