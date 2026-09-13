#pragma once

#include "atcoder_reminder/domain.hpp"

#include <string>

namespace reminder {

std::string makeNotificationContent(const Contest& contest,
                                    const std::string& roleId);

} // namespace reminder
