#include "atcoder_reminder/atcoder_html_parser.hpp"
#include "atcoder_reminder/discord_client.hpp"
#include "atcoder_reminder/message_template.hpp"
#include "atcoder_reminder/reminder_policy.hpp"
#include "atcoder_reminder/state_repository.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unistd.h>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class CapturingHttpClient final : public reminder::HttpClient {
public:
    reminder::HttpResponse get(const std::string&, std::size_t) override {
        return {};
    }

    reminder::HttpResponse postJson(const std::string& url,
                                    const std::string& body) override {
        postedUrl = url;
        postedBody = body;
        return response;
    }

    std::string postedUrl;
    std::string postedBody;
    reminder::HttpResponse response{200, R"({"id":"987654321098765432"})", {}, {}};
};

const char* fixture = R"HTML(
<html><body>
<div id="contest-table-upcoming">
<table><thead><tr><th>開始時刻</th><th>コンテスト名</th><th>時間</th></tr></thead>
<tbody>
<tr><td><a><time class="fixtime fixtime-full">2026-09-19 21:00:00+0900</time></a></td>
<td><a href="/contests/abc476">スポンサー名（AtCoder Beginner Contest 476）</a></td><td>01:40</td></tr>
<tr><td><a><time class="fixtime fixtime-full">2026-09-20 21:00:00+0900</time></a></td>
<td><a href="/contests/arc230">AtCoder Regular Contest++ 230</a></td><td>02:30</td></tr>
<tr><td><a><time class="fixtime fixtime-full">2026-09-26 21:00:00+0900</time></a></td>
<td><a href="/contests/ABC477">ABC 477</a></td><td>01:40</td></tr>
<tr><td><a><time class="fixtime fixtime-full">2026-09-26 21:00:00+0900</time></a></td>
<td><a href="/contests/abc477">ABC 477 duplicate</a></td><td>01:40</td></tr>
</tbody></table>
</div>
</body></html>
)HTML";

void testParserAndMessage() {
    reminder::AtCoderHtmlParser parser;
    const reminder::ParseResult parsed = parser.parse(fixture, 1'700'000'000);
    require(parsed.tableFound, "upcoming table should be found");
    require(parsed.contests.size() == 2, "only ABC rows should be selected");
    require(parsed.contests[0].id == "abc476", "first ABC should be abc476");
    require(parsed.contests[0].endTime - parsed.contests[0].startTime == 6'000,
            "01:40 duration should become 6000 seconds");
    require(parsed.contests[0].url == "https://atcoder.jp/contests/abc476",
            "URL should be canonicalized");
    const std::string conflictingDuplicate = R"HTML(
<div id="contest-table-upcoming"><table><tbody>
<tr><td><time>2026-09-26 21:00:00+0900</time></td><td><a href="/contests/abc477">ABC</a></td><td>01:40</td></tr>
<tr><td><time>2026-09-26 21:00:00+0900</time></td><td><a href="/contests/abc477">ABC</a></td><td>02:00</td></tr>
</tbody></table></div>)HTML";
    const reminder::ParseResult conflicting = parser.parse(
        conflictingDuplicate, 1'700'000'000);
    require(conflicting.tableFound && conflicting.contests.empty(),
            "conflicting duplicate schedules must be rejected");

    const std::string content = reminder::makeNotificationContent(
        parsed.contests[0], "123456789012345678");
    const std::string expected =
        "**<@&123456789012345678>**\n\n"
        "# **AtCoder Beginner Contest 476**\n\n"
        "**本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 476]"
        "(https://atcoder.jp/contests/abc476) が開催されます。**\n\n"
        "**皆さんぜひ参加しましょう！🔥**";
    require(content == expected, "notification content must match the design");
}

void testDiscordRoleMention() {
    CapturingHttpClient http;
    reminder::Config config;
    config.discordWebhookUrl = "https://discord.com/api/webhooks/123456789012345678/test-token";
    config.discordRoleId = "234567890123456789";
    reminder::WebhookDiscordClient discord(http, config);
    std::string error;
    std::string discordMessageId;
    const reminder::SendResult result = discord.send(
        "<@&234567890123456789>\n\nbody",
        discordMessageId, error);
    require(result == reminder::SendResult::Sent, "role mention request should be sent");
    require(http.postedUrl
                == "https://discord.com/api/webhooks/123456789012345678/test-token?wait=true",
            "webhook request must ask Discord to return the created message");
    require(discordMessageId == "987654321098765432",
            "webhook message ID should be captured from Discord response");
    const nlohmann::json request = nlohmann::json::parse(http.postedBody);
    require(request["content"].get<std::string>().rfind(
                "<@&234567890123456789>\n\n", 0) == 0,
            "role mention must be at the beginning of content");
    require(request["allowed_mentions"]["parse"].empty(),
            "all broad mentions must remain disabled");
    require(request["allowed_mentions"]["roles"].size() == 1
                && request["allowed_mentions"]["roles"][0] == "234567890123456789",
            "only the configured role should be allowed to mention");
}

void testPolicy() {
    reminder::Contest contest{"abc1", 1, 10'000, 16'000,
                              "https://atcoder.jp/contests/abc1"};
    reminder::CurrentContest state;
    state.contest = contest;
    state.reminderTime = reminder::reminderTime(contest);
    require(state.reminderTime == 2'800, "reminder must be two hours before start");
    require(!reminder::shouldSendNormal(state, 2'799), "must not send before reminder time");
    require(reminder::shouldSendNormal(state, 2'800), "must send at reminder time");
    require(!reminder::shouldSendNormal(state, 10'000), "must not send after contest starts");
    require(reminder::atcoderCheckIntervalSeconds(&state, 10'000 - 25 * 60 * 60) == 3600,
            "more than 24 hours should use 60 minutes");
    require(reminder::atcoderCheckIntervalSeconds(&state, 10'000 - 12 * 60 * 60) == 900,
            "between 6 and 24 hours should use 15 minutes");
    require(reminder::atcoderCheckIntervalSeconds(&state, 10'000 - 3 * 60 * 60) == 300,
            "within 6 hours should use 5 minutes");
}

void testSaturdayCheck() {
    constexpr reminder::UnixSeconds saturday19Jst = 1'791'626'400; // 2026-10-10 19:00 JST
    require(!reminder::dueSaturdayCheckDate(saturday19Jst - 1).has_value(),
            "Saturday check must not run before 19:00 JST");
    require(reminder::dueSaturdayCheckDate(saturday19Jst) == "2026-10-10",
            "Saturday check should start at 19:00 JST even when the host uses UTC");
    require(reminder::dueSaturdayCheckDate(saturday19Jst + 5 * 3600 - 1) == "2026-10-10",
            "a late Saturday startup must still check the same date");
    require(!reminder::dueSaturdayCheckDate(saturday19Jst + 5 * 3600).has_value(),
            "Saturday check must stop at Sunday midnight JST");
    require(!reminder::dueSaturdayCheckDate(saturday19Jst - 86400).has_value(),
            "Friday must not trigger a Saturday notification");
    require(reminder::dueSaturdayCheckDate(saturday19Jst + 7 * 86400) == "2026-10-17",
            "the following Saturday must use a new completion key");

    CapturingHttpClient http;
    reminder::Config config;
    config.discordWebhookUrl = "https://discord.com/api/webhooks/123456789012345678/test-token";
    config.discordRoleId = "234567890123456789";
    reminder::WebhookDiscordClient discord(http, config);
    std::string messageId;
    std::string error;
    const auto result = discord.send(
        reminder::makeNoContestTodayContent(config.discordRoleId),
        messageId, error);
    require(result == reminder::SendResult::Sent, "weekly notice should use the webhook");
    const auto payload = nlohmann::json::parse(http.postedBody);
    require(payload["content"] == "**<@&234567890123456789>**\n\n**今日ABCは開催されません**",
            "weekly notice must send the exact bold text and configured role");
    require(payload["allowed_mentions"]["parse"].empty()
                && payload["allowed_mentions"]["roles"] == nlohmann::json::array({config.discordRoleId}),
            "weekly notice must only mention the configured role");
}

void testSaturdaySchedule() {
    constexpr reminder::UnixSeconds saturday19Jst = 1'791'626'400;
    reminder::AtCoderHtmlParser parser;
    const std::string tomorrowOnly = R"HTML(
<div id="contest-table-upcoming"><table><tbody>
<tr><td><time>2026-10-11 21:00:00+0900</time></td><td><a href="/contests/abc479">ABC</a></td><td>01:40</td></tr>
</tbody></table></div>)HTML";
    const std::string emptyRecent =
        "<div id=\"contest-table-recent\"><table><tbody></tbody></table></div>";
    const auto absent = parser.parse(tomorrowOnly + emptyRecent, saturday19Jst);
    require(absent.tableFound && !absent.hasInvalidAbcRows && !absent.hasAbcToday,
            "a valid Sunday ABC must not count as a Saturday contest");
    const auto earlierToday = parser.parse(tomorrowOnly + R"HTML(
<div id="contest-table-recent"><table><tbody>
<tr><td><time>2026-10-10 13:00:00+0900</time></td><td><a href="/contests/abc478">ABC</a></td><td>01:40</td></tr>
</tbody></table></div>)HTML", saturday19Jst);
    require(earlierToday.hasAbcToday && !earlierToday.hasInvalidAbcRows,
            "an ABC held earlier on Saturday must suppress the absence notice");
    require(earlierToday.contests.size() == 1 && earlierToday.contests.front().id == "abc479",
            "a past Saturday ABC must not replace the future reminder target");
    const auto running = parser.parse(tomorrowOnly + emptyRecent + R"HTML(
<div id="contest-table-action"><table><tbody>
<tr><td><time>2026-10-10 18:00:00+0900</time></td><td><a href="/contests/abc478">ABC</a></td><td>02:00</td></tr>
</tbody></table></div>)HTML", saturday19Jst);
    require(running.hasAbcToday, "an ongoing Saturday ABC must suppress the absence notice");
    const auto invalid = parser.parse(tomorrowOnly + R"HTML(
<div id="contest-table-recent"><table><tbody>
<tr><td></td><td><a href="/contests/abc478">ABC</a></td><td>01:40</td></tr>
</tbody></table></div>)HTML", saturday19Jst);
    require(invalid.hasInvalidAbcRows,
            "an unreadable ABC row must prevent concluding that no ABC is scheduled");
    const auto empty = parser.parse(
        "<div id=\"contest-table-upcoming\"><table><tbody></tbody></table></div>" + emptyRecent,
        saturday19Jst);
    require(empty.tableFound && !empty.hasAbcToday && !empty.hasInvalidAbcRows,
            "an intact empty schedule should allow an absence notice");
    require(!parser.parse("<html>unavailable</html>", saturday19Jst).tableFound,
            "an unavailable schedule must not be treated as an absence");
    require(parser.parse(tomorrowOnly, saturday19Jst).hasInvalidAbcRows,
            "a missing recent table must not be treated as a confirmed absence");
}

void testNextContestPolicyAndMessage() {
    constexpr reminder::UnixSeconds start = 1'791'720'000; // 2026-10-11 21:00 JST
    reminder::CurrentContest previous;
    previous.contest = {"abc479", 479, start, start + 6000,
                        "https://atcoder.jp/contests/abc479"};
    const reminder::Contest next{"abc480", 480, 1'792'238'400, 1'792'244'400,
                                 "https://atcoder.jp/contests/abc480"};
    const reminder::Contest later{"abc481", 481, next.startTime + 7 * 86400,
                                  next.endTime + 7 * 86400,
                                  "https://atcoder.jp/contests/abc481"};
    const std::vector<reminder::Contest> candidates{later, previous.contest, next};
    require(!reminder::nextContestAfterEnd(previous, candidates, start).has_value(),
            "a next contest notice must not be queued at contest start");
    require(!reminder::nextContestAfterEnd(previous, candidates, previous.contest.endTime - 1).has_value(),
            "the current contest must be retained until its end");
    const auto selected = reminder::nextContestAfterEnd(previous, candidates, previous.contest.endTime);
    require(selected.has_value() && selected->id == "abc480",
            "contest end should select the earliest future ABC");
    require(!reminder::nextContestAfterEnd(previous, {}, previous.contest.endTime).has_value(),
            "an unpublished next contest must not produce a guessed date");
    require(reminder::nextContestAfterEnd(previous, candidates, next.startTime)->id == "abc481",
            "a delayed restart must select a future contest instead of a past date");

    const std::string content = reminder::makeNextContestContent(next);
    require(content == "# **次回のAtCoder Beginner Contest 480**\n\n"
                       "**次回は 2026/10/17 21:00 ～ 22:40（日本時間）に "
                       "[AtCoder Beginner Contest 480](https://atcoder.jp/contests/abc480) "
                       "が開催される予定です。**",
            "next contest notice must include the full JST date and contest link");
    require(content.find('@') == std::string::npos,
            "next contest notice must not contain a mention");
    reminder::Contest overnight = next;
    overnight.startTime += 9000;
    overnight.endTime += 9000;
    require(reminder::makeNextContestContent(overnight).find("2026/10/18 01:10") != std::string::npos,
            "an overnight contest must display the end date as well");

    CapturingHttpClient http;
    reminder::Config config;
    config.discordWebhookUrl = "https://discord.com/api/webhooks/123456789012345678/test-token";
    config.discordRoleId = "234567890123456789";
    reminder::WebhookDiscordClient discord(http, config);
    std::string messageId;
    std::string error;
    require(discord.send(content, messageId, error, false) == reminder::SendResult::Sent,
            "the next contest notice should be sent through the webhook");
    const auto payload = nlohmann::json::parse(http.postedBody);
    require(payload["content"] == content && payload["allowed_mentions"]["parse"].empty()
                && payload["allowed_mentions"]["roles"].empty(),
            "next contest requests must explicitly disallow all mentions");
    http.response = reminder::HttpResponse{500, {}, {}, {}};
    require(discord.send(content, messageId, error, false) == reminder::SendResult::RetryableError,
            "a failed next contest request must remain retryable");
}

void testNextContestPersistence() {
    const auto path = std::filesystem::temp_directory_path()
        / ("atcoder-next-contest-test-" + std::to_string(getpid()) + ".sqlite3");
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    reminder::CurrentContest previous;
    previous.contest = {"abc479", 479, 1'791'720'000, 1'791'726'000,
                        "https://atcoder.jp/contests/abc479"};
    previous.reminderTime = reminder::reminderTime(previous.contest);
    previous.updatedAt = previous.contest.startTime - 7200;
    reminder::CurrentContest next;
    next.contest = {"abc480", 480, 1'792'238'400, 1'792'244'400,
                    "https://atcoder.jp/contests/abc480"};
    next.reminderTime = reminder::reminderTime(next.contest);
    next.updatedAt = previous.contest.endTime;
    std::string error;
    const auto execute = [&](const char* sql) {
        sqlite3* connection = nullptr;
        require(sqlite3_open(path.c_str(), &connection) == SQLITE_OK, "test database should open");
        const int result = sqlite3_exec(connection, sql, nullptr, nullptr, nullptr);
        sqlite3_close(connection);
        require(result == SQLITE_OK, "test SQL should succeed");
    };
    {
        reminder::SqliteStateRepository repository(path.string());
        require(repository.initialize(error), "initial database should initialize");
        const auto key = reminder::notificationKey(reminder::NotificationKind::Normal, previous.contest);
        require(repository.markPending(reminder::NotificationKind::Normal, key, previous, error),
                "existing normal notice should be recorded");
        previous.normalStatus = reminder::NotificationStatus::Sent;
        require(repository.markSent(key, "old-message", previous, error),
                "existing sent notice should be recorded");
        require(repository.markWeeklyCheckCompleted("2026-10-10", error), "existing weekly check should be recorded");
        execute("DROP TABLE next_contest_notices;"); // Reproduce the deployed schema before this feature.
    }
    {
        reminder::SqliteStateRepository repository(path.string());
        require(repository.initialize(error), "the new table should be added to an existing database");
        require(repository.load(error)->normalStatus == reminder::NotificationStatus::Sent,
                "migration must preserve the current contest's sent state");
        bool complete = false;
        require(repository.weeklyCheckCompleted("2026-10-10", complete, error) && complete,
                "migration must preserve weekly completion");
        next.updatedAt = previous.contest.endTime - 1;
        require(!repository.advanceToNextContest(previous, next, error),
                "database transition must reject a preview before contest end");
        next.updatedAt = previous.contest.endTime;
        execute("CREATE TRIGGER reject_advance BEFORE UPDATE ON current_contest "
                "BEGIN SELECT RAISE(FAIL, 'simulated state failure'); END;");
        error.clear();
        require(!repository.advanceToNextContest(previous, next, error),
                "a state write failure must fail the transition");
        require(!repository.loadPendingNextContestNotice(error).has_value() && error.empty(),
                "the notice insert must roll back with the failed state update");
        require(repository.load(error)->contest.id == "abc479", "failed transition must retain the ended contest");
        execute("DROP TRIGGER reject_advance;");
        require(repository.advanceToNextContest(previous, next, error), "valid transition should save state and pending notice");
        require(repository.load(error)->contest.id == "abc480"
                    && repository.load(error)->normalStatus == reminder::NotificationStatus::Pending,
                "the next contest's normal reminder must remain independently pending");
    }
    {
        reminder::SqliteStateRepository repository(path.string());
        require(repository.initialize(error), "saved pending notice should reopen");
        const auto notice = repository.loadPendingNextContestNotice(error);
        require(notice.has_value() && notice->contest.id == "abc480"
                    && notice->contest.startTime == next.contest.startTime,
                "an unsent notice must survive a restart with its full schedule");
        reminder::Contest postponed = notice->contest;
        postponed.startTime += 86400;
        postponed.endTime += 86400;
        require(repository.refreshNextContestNotice(notice->key, postponed, error),
                "a retry must be able to refresh a changed official schedule");
        require(repository.loadPendingNextContestNotice(error)->contest.startTime == postponed.startTime,
                "the refreshed preview schedule must be persisted before posting");
        require(repository.markNextContestNoticeSent(notice->key, "next-message", error),
                "successful notice should be completed");
        reminder::CurrentContest reminded = next;
        reminded.normalStatus = reminder::NotificationStatus::Sent;
        require(repository.save(reminded, error), "a later normal reminder should be recordable");
        require(repository.advanceToNextContest(previous, next, error), "repeated transition should be idempotent");
        require(repository.load(error)->normalStatus == reminder::NotificationStatus::Sent,
                "a repeated transition must not reset the normal reminder's sent state");
        require(!repository.loadPendingNextContestNotice(error).has_value(),
                "a sent notice must not be queued again by a repeated transition");
        reminder::CurrentContest later = next;
        later.contest.id = "abc481";
        later.contest.number = 481;
        later.contest.startTime += 7 * 86400;
        later.contest.endTime += 7 * 86400;
        later.updatedAt = next.contest.endTime;
        require(repository.advanceToNextContest(next, later, error), "a later contest should have its own notice");
        const auto stale = repository.loadPendingNextContestNotice(error);
        require(stale.has_value(), "a later notice should be pending");
        require(repository.markNextContestNoticeSkipped(stale->key, error), "an expired preview should be skippable");
    }
    {
        reminder::SqliteStateRepository repository(path.string());
        require(repository.initialize(error) && !repository.loadPendingNextContestNotice(error).has_value(),
                "completed and expired previews must not reappear after restart");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
}

void testStateRepository() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("atcoder-abc-reminder-test-" + std::to_string(getpid()) + ".sqlite3");
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");

    reminder::SqliteStateRepository repository(path.string());
    std::string error;
    require(repository.initialize(error), "SQLite initialization should succeed");
    reminder::Contest contest{"abc476", 476, 1'800'000'000, 1'800'006'000,
                              "https://atcoder.jp/contests/abc476"};
    reminder::CurrentContest state;
    state.contest = contest;
    state.reminderTime = reminder::reminderTime(contest);
    state.updatedAt = 1'700'000'000;
    state.lastFetchedAt = state.updatedAt;
    const std::string key = reminder::notificationKey(reminder::NotificationKind::Normal, contest);
    state.pendingKind = reminder::NotificationKind::Normal;
    state.pendingMessageKey = key;
    require(repository.markPending(reminder::NotificationKind::Normal, key, state, error),
            "pending state should be saved");
    const auto loaded = repository.load(error);
    require(loaded.has_value(), "saved state should load");
    require(loaded->pendingMessageKey == key, "pending message key should survive reload");
    state.normalStatus = reminder::NotificationStatus::Sent;
    state.normalSentForStartTime = contest.startTime;
    state.pendingKind.reset();
    state.pendingMessageKey.clear();
    require(repository.markSent(key, "message-id", state, error),
            "sent state should be saved");
    const auto sent = repository.load(error);
    require(sent.has_value() && sent->normalStatus == reminder::NotificationStatus::Sent,
            "sent state should survive reload");
    bool weeklyCompleted = true;
    require(repository.weeklyCheckCompleted("2026-10-10", weeklyCompleted, error)
                && !weeklyCompleted,
            "a fresh Saturday must not be marked complete");
    require(repository.markWeeklyCheckCompleted("2026-10-10", error),
            "a successful Saturday check should be saved");
    require(repository.markWeeklyCheckCompleted("2026-10-10", error),
            "saving the same Saturday twice should be idempotent");
    {
        reminder::SqliteStateRepository reopened(path.string());
        require(reopened.initialize(error), "a second connection should open the saved database");
        require(reopened.weeklyCheckCompleted("2026-10-10", weeklyCompleted, error)
                    && weeklyCompleted,
                "Saturday completion must survive reopening the database");
        require(reopened.weeklyCheckCompleted("2026-10-17", weeklyCompleted, error)
                    && !weeklyCompleted,
                "the next Saturday must remain pending");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
}

void testLiveHtml(const char* path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "live AtCoder HTML fixture should be readable");
    const std::string html((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    reminder::AtCoderHtmlParser parser;
    const reminder::ParseResult parsed = parser.parse(
        html, reminder::currentUnixSeconds());
    require(parsed.tableFound, "live AtCoder page should contain the upcoming table");
    require(!parsed.hasInvalidAbcRows, "live AtCoder schedule should be completely readable");
    require(!parsed.contests.empty(), "live AtCoder page should contain a future ABC");
    for (const reminder::Contest& contest : parsed.contests) {
        require(contest.id.rfind("abc", 0) == 0,
                "live parser must return ABC contests only");
        require(contest.endTime > contest.startTime,
                "live parser must calculate a positive contest duration");
    }
    std::cout << "Live HTML parsed: " << parsed.contests.size() << " future ABC row(s)\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        testParserAndMessage();
        testDiscordRoleMention();
        testPolicy();
        testSaturdayCheck();
        testSaturdaySchedule();
        testNextContestPolicyAndMessage();
        testNextContestPersistence();
        testStateRepository();
        if (argc > 1) testLiveHtml(argv[1]);
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
