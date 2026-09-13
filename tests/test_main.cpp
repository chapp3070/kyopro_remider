#include "atcoder_reminder/atcoder_html_parser.hpp"
#include "atcoder_reminder/message_template.hpp"
#include "atcoder_reminder/reminder_policy.hpp"
#include "atcoder_reminder/state_repository.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

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

    const std::string content = reminder::makeNotificationContent(parsed.contests[0]);
    const std::string expected =
        "# AtCoder Beginner Contest 476\n\n"
        "本日 21:00 ～ 22:40 に [AtCoder Beginner Contest 476]"
        "(https://atcoder.jp/contests/abc476) が開催されます。\n\n"
        "皆さんぜひ参加しましょう！🔥";
    require(content == expected, "notification content must match the design");
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

void testStateRepository() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "atcoder-abc-reminder-test.sqlite3";
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
        testPolicy();
        testStateRepository();
        if (argc > 1) testLiveHtml(argv[1]);
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
