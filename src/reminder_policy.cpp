#include "atcoder_reminder/reminder_policy.hpp"

#include <algorithm>

namespace reminder {

UnixSeconds reminderTime(const Contest& contest) {
    return contest.startTime - kReminderBeforeSeconds;
}

bool shouldSendNormal(const CurrentContest& state, UnixSeconds now) {
    return state.normalStatus == NotificationStatus::Pending
        && now >= state.reminderTime
        && now < state.contest.startTime;
}

std::optional<Contest> nextContestAfterEnd(const CurrentContest& state,
                                         const std::vector<Contest>& contests,
                                         UnixSeconds now) {
    if (now < state.contest.endTime) return std::nullopt;
    std::optional<Contest> next;
    for (const Contest& contest : contests) {
        if (contest.id == state.contest.id || contest.startTime <= now
            || contest.endTime <= contest.startTime) continue;
        if (!next.has_value() || contest.startTime < next->startTime
            || (contest.startTime == next->startTime && contest.id < next->id)) {
            next = contest;
        }
    }
    return next;
}

std::int64_t atcoderCheckIntervalSeconds(const CurrentContest* state,
                                         UnixSeconds now) {
    if (state == nullptr || state->contest.startTime <= now) {
        return 3600;
    }
    const UnixSeconds remaining = state->contest.startTime - now;
    if (remaining > 24 * 60 * 60) {
        return 3600;
    }
    if (remaining > 6 * 60 * 60) {
        return 15 * 60;
    }
    return 5 * 60;
}

std::string notificationKey(NotificationKind kind, const Contest& contest) {
    const char* prefix = kind == NotificationKind::Normal ? "normal" : "change";
    return std::string(prefix) + ":" + contest.id + ":" + std::to_string(contest.startTime);
}

} // namespace reminder
