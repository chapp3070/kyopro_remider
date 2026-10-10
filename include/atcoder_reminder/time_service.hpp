#pragma once

#include "atcoder_reminder/domain.hpp"

#include <optional>
#include <string>

namespace reminder {

UnixSeconds currentUnixSeconds();
bool isPlausibleUnixTime(UnixSeconds now);
std::string jstDateKey(UnixSeconds timestamp);
std::optional<std::string> dueSaturdayCheckDate(UnixSeconds now);

} // namespace reminder
