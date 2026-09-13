#include "atcoder_reminder/time_service.hpp"

#include <chrono>

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

} // namespace reminder
