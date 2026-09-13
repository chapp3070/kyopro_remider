#include "atcoder_reminder/message_template.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace reminder {
namespace {

std::string formatJst(UnixSeconds epoch) {
    // Japan Standard Time is UTC+09:00 and has no DST.
    const std::time_t shifted = static_cast<std::time_t>(epoch + 9 * 60 * 60);
    std::tm tm{};
    if (gmtime_r(&shifted, &tm) == nullptr) {
        return "--:--";
    }
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << tm.tm_hour
           << ":" << std::setw(2) << tm.tm_min;
    return output.str();
}

} // namespace

std::string makeNotificationContent(const Contest& contest,
                                    const std::string& roleId) {
    const std::string label = "AtCoder Beginner Contest " + std::to_string(contest.number);
    const std::string url = "https://atcoder.jp/contests/" + contest.id;
    return "<@&" + roleId + ">\n\n"
        + "# " + label + "\n\n"
        + "本日 " + formatJst(contest.startTime) + " ～ "
        + formatJst(contest.endTime) + " に [" + label + "](" + url
        + ") が開催されます。\n\n"
        + "皆さんぜひ参加しましょう！🔥";
}

} // namespace reminder
