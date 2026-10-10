#include "atcoder_reminder/atcoder_html_parser.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xpath.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <regex>
#include <stdexcept>
#include <utility>

namespace reminder {
namespace {

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string nodeText(xmlNodePtr node) {
    xmlChar* content = xmlNodeGetContent(node);
    if (content == nullptr) {
        return {};
    }
    std::string result(reinterpret_cast<const char*>(content));
    xmlFree(content);
    return trim(result);
}

std::string property(xmlNodePtr node, const char* name) {
    xmlChar* value = xmlGetProp(node, BAD_CAST name);
    if (value == nullptr) {
        return {};
    }
    std::string result(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return result;
}

std::int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

std::optional<UnixSeconds> parseTimestamp(const std::string& value) {
    static const std::regex pattern(
        R"(^([0-9]{4})-([0-9]{2})-([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2})([+-])([0-9]{2})([0-9]{2})$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        return std::nullopt;
    }

    const int year = std::stoi(match[1].str());
    const unsigned month = static_cast<unsigned>(std::stoi(match[2].str()));
    const unsigned day = static_cast<unsigned>(std::stoi(match[3].str()));
    const unsigned hour = static_cast<unsigned>(std::stoi(match[4].str()));
    const unsigned minute = static_cast<unsigned>(std::stoi(match[5].str()));
    const unsigned second = static_cast<unsigned>(std::stoi(match[6].str()));
    const unsigned offsetHour = static_cast<unsigned>(std::stoi(match[8].str()));
    const unsigned offsetMinute = static_cast<unsigned>(std::stoi(match[9].str()));

    const bool leapYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const unsigned daysInMonth = month == 2 ? (leapYear ? 29U : 28U)
        : ((month == 4 || month == 6 || month == 9 || month == 11) ? 30U : 31U);
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > daysInMonth
        || hour > 23 || minute > 59 || second > 59 || offsetHour > 23 || offsetMinute > 59) {
        return std::nullopt;
    }

    // Verify that the civil date did not normalize across a month boundary.
    const std::int64_t dayNumber = daysFromCivil(year, month, day);
    if (daysFromCivil(year, month, 1) + static_cast<std::int64_t>(day - 1) != dayNumber) {
        return std::nullopt;
    }
    const std::int64_t local = dayNumber * 86400
        + static_cast<std::int64_t>(hour) * 3600
        + static_cast<std::int64_t>(minute) * 60
        + second;
    const std::int64_t offset =
        (static_cast<std::int64_t>(offsetHour) * 60 + offsetMinute) * 60;
    return local - (match[7].str() == "+" ? offset : -offset);
}

std::optional<std::int64_t> parseDuration(const std::string& value) {
    static const std::regex pattern(R"(^([0-9]{1,3}):([0-9]{2})$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        return std::nullopt;
    }
    const std::int64_t hours = std::stoll(match[1].str());
    const std::int64_t minutes = std::stoll(match[2].str());
    if (minutes > 59) {
        return std::nullopt;
    }
    return hours * 3600 + minutes * 60;
}

std::optional<Contest> parseRow(xmlNodePtr row, UnixSeconds now,
                                bool& hasInvalidAbcRows) {
    xmlXPathContextPtr context = xmlXPathNewContext(row->doc);
    if (context == nullptr) {
        hasInvalidAbcRows = true;
        return std::nullopt;
    }
    context->node = row;

    xmlXPathObjectPtr links = xmlXPathEvalExpression(
        BAD_CAST "./td[2]//a[@href]", context);
    xmlXPathObjectPtr times = xmlXPathEvalExpression(
        BAD_CAST "./td[1]//time", context);
    xmlXPathObjectPtr durations = xmlXPathEvalExpression(
        BAD_CAST "./td[3]", context);

    std::optional<Contest> result;
    bool recognizedAbc = false;
    bool validAbc = false;
    static const std::regex idPattern(R"(^/contests/(abc([0-9]+))$)",
                                       std::regex_constants::icase);
    if (links != nullptr && links->nodesetval != nullptr) {
        for (int index = 0; index < links->nodesetval->nodeNr; ++index) {
            const std::string href = property(links->nodesetval->nodeTab[index], "href");
            if (std::regex_match(href, idPattern)) {
                recognizedAbc = true;
            }
        }
    }
    if (links != nullptr && links->nodesetval != nullptr && links->nodesetval->nodeNr == 1) {
        const xmlNodePtr link = links->nodesetval->nodeTab[0];
        const std::string href = property(link, "href");
        std::smatch idMatch;
        if (std::regex_match(href, idMatch, idPattern)
            && times != nullptr && times->nodesetval != nullptr
            && times->nodesetval->nodeNr == 1
            && durations != nullptr && durations->nodesetval != nullptr
            && durations->nodesetval->nodeNr == 1) {
            const std::string timeText = nodeText(times->nodesetval->nodeTab[0]);
            const std::string durationText = nodeText(durations->nodesetval->nodeTab[0]);
            const auto start = parseTimestamp(timeText);
            const auto duration = parseDuration(durationText);
            if (start.has_value() && duration.has_value() && *duration > 0
                && *start <= std::numeric_limits<UnixSeconds>::max() - *duration) {
                const std::string numberText = idMatch[2].str();
                long long number = 0;
                const auto conversion = std::from_chars(
                    numberText.data(), numberText.data() + numberText.size(), number);
                if (conversion.ec == std::errc{} && conversion.ptr == numberText.data() + numberText.size()
                    && number > 0 && number <= std::numeric_limits<int>::max()) {
                    validAbc = true;
                    Contest contest;
                    contest.id = idMatch[1].str();
                    std::transform(contest.id.begin(), contest.id.end(), contest.id.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    contest.number = static_cast<int>(number);
                    contest.startTime = *start;
                    contest.endTime = *start + *duration;
                    contest.url = "https://atcoder.jp/contests/" + contest.id;
                    if (*start > now) result = contest;
                }
            }
        }
    }

    if (links != nullptr) xmlXPathFreeObject(links);
    if (times != nullptr) xmlXPathFreeObject(times);
    if (durations != nullptr) xmlXPathFreeObject(durations);
    xmlXPathFreeContext(context);
    if (recognizedAbc && !validAbc) hasInvalidAbcRows = true;
    return result;
}

} // namespace

ParseResult AtCoderHtmlParser::parse(const std::string& html, UnixSeconds now) const {
    ParseResult result;
    if (html.empty()) {
        result.error = "empty HTML";
        return result;
    }
    if (html.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        result.error = "HTML exceeds libxml2 input size limit";
        return result;
    }

    htmlDocPtr document = htmlReadMemory(
        html.data(), static_cast<int>(html.size()), "atcoder.html", nullptr,
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (document == nullptr) {
        result.error = "libxml2 could not parse HTML";
        return result;
    }

    xmlXPathContextPtr context = xmlXPathNewContext(document);
    if (context == nullptr) {
        xmlFreeDoc(document);
        result.error = "could not create XPath context";
        return result;
    }

    xmlXPathObjectPtr table = xmlXPathEvalExpression(
        BAD_CAST "//*[@id='contest-table-upcoming']", context);
    if (table == nullptr || table->nodesetval == nullptr || table->nodesetval->nodeNr == 0) {
        if (table != nullptr) xmlXPathFreeObject(table);
        xmlXPathFreeContext(context);
        xmlFreeDoc(document);
        result.error = "upcoming contest table not found";
        return result;
    }
    result.tableFound = true;
    xmlXPathFreeObject(table);

    xmlXPathObjectPtr rows = xmlXPathEvalExpression(
        BAD_CAST "//*[@id='contest-table-upcoming']//tbody/tr[td]", context);
    if (rows != nullptr && rows->nodesetval != nullptr) {
        for (int index = 0; index < rows->nodesetval->nodeNr; ++index) {
            const auto candidate = parseRow(rows->nodesetval->nodeTab[index], now,
                                            result.hasInvalidAbcRows);
            if (candidate.has_value()) {
                result.contests.push_back(*candidate);
            }
        }
    }
    if (rows != nullptr) xmlXPathFreeObject(rows);

    // Check the whole JST date without adding past ABCs to reminder targets.
    const UnixSeconds todayStart = now - (now + 9 * 3600) % 86400;
    const std::string today = jstDateKey(now);
    // The ongoing section is omitted when no contest is running. Upcoming and
    // recent tables must have the expected structure before absence is known.
    xmlXPathObjectPtr scheduleTables = xmlXPathEvalExpression(
        BAD_CAST "//*[@id='contest-table-upcoming' or @id='contest-table-recent']//table/tbody",
        context);
    if (scheduleTables == nullptr || scheduleTables->nodesetval == nullptr
        || scheduleTables->nodesetval->nodeNr != 2) {
        result.hasInvalidAbcRows = true;
    }
    if (scheduleTables != nullptr) xmlXPathFreeObject(scheduleTables);
    xmlXPathObjectPtr dailyRows = xmlXPathEvalExpression(
        BAD_CAST "//*[@id='contest-table-upcoming' or @id='contest-table-action' "
                 "or @id='contest-table-recent']//tbody/tr[td]", context);
    if (dailyRows == nullptr) {
        result.hasInvalidAbcRows = true;
    } else if (dailyRows->nodesetval != nullptr) {
        for (int index = 0; index < dailyRows->nodesetval->nodeNr; ++index) {
            const auto candidate = parseRow(dailyRows->nodesetval->nodeTab[index],
                                            todayStart - 1,
                                            result.hasInvalidAbcRows);
            if (candidate.has_value() && jstDateKey(candidate->startTime) == today) {
                result.hasAbcToday = true;
            }
        }
    }
    if (dailyRows != nullptr) xmlXPathFreeObject(dailyRows);
    xmlXPathFreeContext(context);
    xmlFreeDoc(document);

    std::vector<Contest> deduplicated;
    std::vector<std::string> conflictingIds;
    for (const Contest& candidate : result.contests) {
        const auto existing = std::find_if(
            deduplicated.begin(), deduplicated.end(), [&candidate](const Contest& value) {
                return value.id == candidate.id;
            });
        if (existing == deduplicated.end()) {
            deduplicated.push_back(candidate);
            continue;
        }
        const bool sameSchedule = existing->number == candidate.number
            && existing->startTime == candidate.startTime
            && existing->endTime == candidate.endTime
            && existing->url == candidate.url;
        if (!sameSchedule
            && std::find(conflictingIds.begin(), conflictingIds.end(), candidate.id)
                == conflictingIds.end()) {
            conflictingIds.push_back(candidate.id);
        }
    }
    deduplicated.erase(
        std::remove_if(deduplicated.begin(), deduplicated.end(), [&conflictingIds](const Contest& value) {
            return std::find(conflictingIds.begin(), conflictingIds.end(), value.id)
                != conflictingIds.end();
        }),
        deduplicated.end());
    if (!conflictingIds.empty()) result.hasInvalidAbcRows = true;
    std::sort(deduplicated.begin(), deduplicated.end(),
              [](const Contest& left, const Contest& right) {
                  if (left.startTime != right.startTime) return left.startTime < right.startTime;
                  return left.id < right.id;
              });
    result.contests = std::move(deduplicated);
    if (result.contests.empty()) {
        result.error = "no valid future ABC row";
    }
    return result;
}

} // namespace reminder
