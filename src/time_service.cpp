#include "atcoder_reminder/time_service.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace reminder {

UnixSeconds currentUnixSeconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

bool isPlausibleUnixTime(UnixSeconds now) {
    // Reject an uninitialized epoch and obviously corrupted clocks.
    constexpr UnixSeconds kMinimum = 1'700'000'000; // 2023-11-14 UTC
    constexpr UnixSeconds kMaximum = 4'102'444'800; // 2100-01-01 UTC
    return now >= kMinimum && now < kMaximum;
}

std::string jstDateKey(UnixSeconds timestamp) {
    const std::time_t shifted = static_cast<std::time_t>(timestamp + 9 * 60 * 60);
    std::tm local{};
    if (gmtime_r(&shifted, &local) == nullptr) return {};
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << local.tm_year + 1900 << '-'
           << std::setw(2) << local.tm_mon + 1 << '-'
           << std::setw(2) << local.tm_mday;
    return output.str();
}

std::optional<std::string> dueSaturdayCheckDate(UnixSeconds now) {
    const std::time_t shifted = static_cast<std::time_t>(now + 9 * 60 * 60);
    std::tm local{};
    if (gmtime_r(&shifted, &local) == nullptr
        || local.tm_wday != 6 || local.tm_hour < 19) {
        return std::nullopt;
    }
    return jstDateKey(now);
}

} // namespace reminder
