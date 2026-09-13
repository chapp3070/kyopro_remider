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
