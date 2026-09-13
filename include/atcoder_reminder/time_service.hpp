#pragma once

#include "atcoder_reminder/domain.hpp"

namespace reminder {

UnixSeconds currentUnixSeconds();
bool isPlausibleUnixTime(UnixSeconds now);

} // namespace reminder
