#pragma once

#include "atcoder_reminder/domain.hpp"

#include <string>

namespace reminder {

std::string makeNotificationContent(const Contest& contest,
                                    const std::string& roleId);
std::string makeNoContestTodayContent(const std::string& roleId);
std::string makeNextContestContent(const Contest& contest);

} // namespace reminder
