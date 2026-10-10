#include "atcoder_reminder/message_template.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace reminder {
namespace {

std::string formatJst(UnixSeconds epoch, const char* format = "%H:%M") {
    // Japan Standard Time is UTC+09:00 and has no DST.
    const std::time_t shifted = static_cast<std::time_t>(epoch + 9 * 60 * 60);
    std::tm tm{};
    if (gmtime_r(&shifted, &tm) == nullptr) {
        return "--:--";
    }
    std::ostringstream output;
    output << std::put_time(&tm, format);
    return output.str();
}

} // namespace

std::string makeNotificationContent(const Contest& contest,
                                    const std::string& roleId) {
    const std::string label = "AtCoder Beginner Contest " + std::to_string(contest.number);
    const std::string url = "https://atcoder.jp/contests/" + contest.id;
    return "**<@&" + roleId + ">**\n\n"
        + "# **" + label + "**\n\n"
        + "**本日 " + formatJst(contest.startTime) + " ～ "
        + formatJst(contest.endTime) + " に [" + label + "](" + url
        + ") が開催されます。**\n\n"
        + "**皆さんぜひ参加しましょう！🔥**";
}

std::string makeNoContestTodayContent(const std::string& roleId) {
    return "**<@&" + roleId + ">**\n\n**今日ABCは開催されません**";
}

std::string makeNextContestContent(const Contest& contest) {
    const std::string label = "AtCoder Beginner Contest " + std::to_string(contest.number);
    const char* endFormat = jstDateKey(contest.startTime) == jstDateKey(contest.endTime)
        ? "%H:%M" : "%Y/%m/%d %H:%M";
    return "# **次回の" + label + "**\n\n"
        + "**次回は " + formatJst(contest.startTime, "%Y/%m/%d %H:%M") + " ～ "
        + formatJst(contest.endTime, endFormat) + "（日本時間）に [" + label
        + "](https://atcoder.jp/contests/" + contest.id + ") が開催される予定です。**";
}

} // namespace reminder
