#pragma once

#include "atcoder_reminder/domain.hpp"

#include <cstdint>
#include <string>

namespace reminder {

constexpr UnixSeconds kReminderBeforeSeconds = 2 * 60 * 60;

UnixSeconds reminderTime(const Contest& contest);
bool shouldSendNormal(const CurrentContest& state, UnixSeconds now);
std::int64_t atcoderCheckIntervalSeconds(const CurrentContest* state,
                                         UnixSeconds now);
std::string notificationKey(NotificationKind kind, const Contest& contest);

} // namespace reminder
