#include "atcoder_reminder/atcoder_client.hpp"
#include "atcoder_reminder/config.hpp"
#include "atcoder_reminder/discord_client.hpp"
#include "atcoder_reminder/message_template.hpp"
#include "atcoder_reminder/reminder_policy.hpp"
#include "atcoder_reminder/state_repository.hpp"
#include "atcoder_reminder/time_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>

namespace {

using namespace reminder;

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

void log(const char* event, const std::string& detail = {}) {
    std::cerr << "[" << event << "]";
    if (!detail.empty()) std::cerr << " " << detail;
    std::cerr << '\n';
}

std::optional<Contest> findContest(const std::vector<Contest>& contests,
                                   const std::string& id) {
    for (const Contest& contest : contests) {
        if (contest.id == id) return contest;
    }
    return std::nullopt;
}

CurrentContest newState(const Contest& contest, UnixSeconds now) {
    CurrentContest state;
    state.contest = contest;
    state.reminderTime = reminderTime(contest);
    state.normalStatus = NotificationStatus::Pending;
    state.changeStatus = NotificationStatus::None;
    state.lastFetchedAt = now;
    state.updatedAt = now;
    return state;
}

bool updateFromAtCoder(CurrentContest& state,
                       const std::vector<Contest>& contests,
                       UnixSeconds now) {
    const auto current = findContest(contests, state.contest.id);
    if (!current.has_value()) {
        return state.contest.endTime <= now;
    }

    const bool changed = current->startTime != state.contest.startTime
        || current->endTime != state.contest.endTime;
    state.lastFetchedAt = now;
    state.updatedAt = now;
    if (!changed) return false;

    state.contest = *current;
    state.reminderTime = reminderTime(*current);
    if (state.normalStatus == NotificationStatus::Pending) {
        // A pending normal notification for the old schedule is no longer valid.
        state.changeStatus = NotificationStatus::None;
        state.changeSentForStartTime.reset();
        state.pendingKind.reset();
        state.pendingMessageKey.clear();
    } else {
        state.changeStatus = NotificationStatus::Pending;
        state.pendingKind = NotificationKind::ScheduleChanged;
        state.pendingMessageKey = notificationKey(NotificationKind::ScheduleChanged,
                                                   state.contest);
    }
    return true;
}

void clearPending(CurrentContest& state) {
    state.pendingKind.reset();
    state.pendingMessageKey.clear();
}

bool markSentState(CurrentContest& state, NotificationKind kind) {
    if (kind == NotificationKind::Normal) {
        state.normalStatus = NotificationStatus::Sent;
        state.normalSentForStartTime = state.contest.startTime;
    } else {
        state.changeStatus = NotificationStatus::Sent;
        state.changeSentForStartTime = state.contest.startTime;
    }
    clearPending(state);
    state.updatedAt = currentUnixSeconds();
    return true;
}

std::optional<NotificationKind> pendingKind(const CurrentContest& state) {
    if (state.pendingKind.has_value()) return state.pendingKind;
    if (state.changeStatus == NotificationStatus::Pending) {
        return NotificationKind::ScheduleChanged;
    }
    return std::nullopt;
}

} // namespace

int run() {
    Config config;
    config.discordWebhookUrl = environment("ATCODER_DISCORD_WEBHOOK_URL");
    config.discordRoleId = environment("ATCODER_DISCORD_ROLE_ID");
    const std::string configuredDb = environment("ATCODER_STATE_DB");
    if (!configuredDb.empty()) config.stateDbPath = configuredDb;

    if (config.discordWebhookUrl.empty() || config.discordRoleId.empty()) {
        log("CONFIG_ERROR", "ATCODER_DISCORD_WEBHOOK_URL and ATCODER_DISCORD_ROLE_ID are required");
        return 2;
    }

    CurlHttpClient http(config);
    AtCoderClient atcoder(http, config);
    WebhookDiscordClient discord(http, config);
    SqliteStateRepository repository(config.stateDbPath);

    std::string error;
    if (!repository.initialize(error)) {
        log("STATE_INIT_FAILED", error);
        return 1;
    }
    std::optional<CurrentContest> state = repository.load(error);
    if (!error.empty()) {
        log("STATE_LOAD_FAILED", error);
        return 1;
    }
    log(state.has_value() ? "STATE_LOAD_OK" : "STATE_EMPTY");

    UnixSeconds nextFetch = 0;
    UnixSeconds nextNotificationAction = 0;
    UnixSeconds nextContestNoticeAction = 0;
    std::int64_t fetchFailures = 0;
    std::string weeklyFetchRequestedDate;
    std::string finishedContestFetchRequestedId;
    std::vector<Contest> confirmedFutureContests;

    while (true) {
        const UnixSeconds now = currentUnixSeconds();
        bool allowNotificationAction = true;
        if (!isPlausibleUnixTime(now)) {
            log("TIME_INVALID");
            std::this_thread::sleep_for(std::chrono::minutes(1));
            continue;
        }

        if (state.has_value() && state->contest.endTime <= now
            && finishedContestFetchRequestedId != state->contest.id) {
            // Request the next contest once, then respect fetch/send backoff
            // even when no future ABC is available yet.
            nextFetch = now;
            finishedContestFetchRequestedId = state->contest.id;
        }

        const std::optional<std::string> saturdayDate = dueSaturdayCheckDate(now);
        bool weeklyCheckPending = false;
        if (saturdayDate.has_value()) {
            bool completed = false;
            if (!repository.weeklyCheckCompleted(*saturdayDate, completed, error)) {
                log("WEEKLY_CHECK_STATE_FAILED", error);
            } else if (!completed) {
                weeklyCheckPending = true;
                if (weeklyFetchRequestedDate != *saturdayDate) {
                    nextFetch = now;
                    weeklyFetchRequestedDate = *saturdayDate;
                }
            }
        }

        if (now >= nextFetch) {
            const ParseResult fetched = atcoder.fetch(now);
            confirmedFutureContests = fetched.tableFound && !fetched.hasInvalidAbcRows
                ? fetched.contests : std::vector<Contest>{};
            const bool validEmptyWeeklySchedule = weeklyCheckPending
                && fetched.tableFound && fetched.contests.empty()
                && !fetched.hasInvalidAbcRows
                && fetched.error == "no valid future ABC row";
            // An empty result is not treated as a valid schedule. Keeping an
            // old future contest after a parser/site change could cause a
            // notification for a stale time, so retry and do not send in this
            // iteration until a complete ABC candidate is available again.
            if (!fetched.tableFound || (fetched.contests.empty() && !validEmptyWeeklySchedule)) {
                allowNotificationAction = false;
                ++fetchFailures;
                const std::int64_t backoff = std::min<std::int64_t>(3600, 300LL << std::min<std::int64_t>(fetchFailures - 1, 3));
                nextFetch = now + backoff;
                if (weeklyCheckPending && fetched.tableFound && fetched.hasInvalidAbcRows) {
                    nextFetch = now + 300;
                }
                log("ATCODER_FETCH_FAILED",
                    fetched.error.empty() ? "no complete future ABC candidate" : fetched.error);
            } else {
                fetchFailures = 0;
                nextFetch = now + 60 * 60;
                bool stateChanged = false;
                if (fetched.contests.empty()) {
                    // A valid empty weekly schedule does not replace or clear
                    // the persisted reminder target.
                    allowNotificationAction = false;
                } else if (!state.has_value()) {
                    state = newState(fetched.contests.front(), now);
                    stateChanged = true;
                    log("CONTEST_SELECTED", state->contest.id);
                } else if (state.has_value()) {
                    const bool currentContestMissing =
                        !findContest(fetched.contests, state->contest.id).has_value();
                    const bool shouldMove = updateFromAtCoder(*state, fetched.contests, now);
                    if (currentContestMissing && state->contest.startTime > now) {
                        // A valid-looking page without the persisted future
                        // contest may represent a cancellation or a changed
                        // page structure. Do not notify from stale state.
                        allowNotificationAction = false;
                        log("CONTEST_NOT_CONFIRMED", state->contest.id);
                    }
                    nextFetch = now + atcoderCheckIntervalSeconds(&*state, now);
                    const auto next = currentContestMissing && !fetched.hasInvalidAbcRows
                        ? nextContestAfterEnd(*state, fetched.contests, now) : std::nullopt;
                    if (next.has_value()) {
                        CurrentContest nextState = newState(*next, now);
                        if (repository.advanceToNextContest(*state, nextState, error)) {
                            state = std::move(nextState);
                            nextFetch = now + atcoderCheckIntervalSeconds(&*state, now);
                            log("CONTEST_FINISHED", "queued next notice for " + state->contest.id);
                        } else {
                            allowNotificationAction = false;
                            nextFetch = now + 300;
                            log("NEXT_CONTEST_STATE_FAILED", error);
                        }
                    } else if (currentContestMissing && state->contest.endTime <= now) {
                        allowNotificationAction = false;
                        nextFetch = now + 300;
                        log("NEXT_CONTEST_SCHEDULE_INVALID", state->contest.id);
                    } else {
                        stateChanged = shouldMove;
                    }
                }
                if (stateChanged && state.has_value()) {
                    state->updatedAt = now;
                    if (!repository.save(*state, error)) log("STATE_SAVE_FAILED", error);
                }

                if (weeklyCheckPending && fetched.hasInvalidAbcRows) {
                    log("WEEKLY_CHECK_SCHEDULE_INVALID", *saturdayDate);
                    nextFetch = now + 300;
                } else if (weeklyCheckPending) {
                    if (fetched.hasAbcToday) {
                        if (!repository.markWeeklyCheckCompleted(*saturdayDate, error)) {
                            log("WEEKLY_CHECK_STATE_FAILED", error);
                            nextFetch = now + 300;
                        } else {
                            log("WEEKLY_CHECK_CONTEST_FOUND", *saturdayDate);
                        }
                    } else {
                        std::string sendError;
                        std::string discordMessageId;
                        const SendResult result = discord.send(
                            makeNoContestTodayContent(config.discordRoleId),
                            discordMessageId, sendError);
                        if (result == SendResult::Sent) {
                            if (!repository.markWeeklyCheckCompleted(*saturdayDate, error)) {
                                log("WEEKLY_CHECK_STATE_FAILED", error);
                                nextFetch = now + 300;
                            } else {
                                log("WEEKLY_NO_ABC_NOTICE_SENT", *saturdayDate);
                            }
                        } else {
                            log("WEEKLY_NO_ABC_NOTICE_RETRY", sendError);
                            nextFetch = now + 300;
                        }
                    }
                }
            }
        }

        if (now >= nextContestNoticeAction) {
            const auto notice = repository.loadPendingNextContestNotice(error);
            if (!error.empty()) {
                log("NEXT_CONTEST_STATE_FAILED", error);
                nextContestNoticeAction = now + 300;
            } else if (notice.has_value()) {
                const auto confirmed = findContest(confirmedFutureContests, notice->contest.id);
                const auto retry = [&](UnixSeconds delay) {
                    nextContestNoticeAction = now + delay;
                    nextFetch = std::min(nextFetch, nextContestNoticeAction);
                };
                if (!confirmed.has_value() && notice->contest.startTime <= now) {
                    if (repository.markNextContestNoticeSkipped(notice->key, error)) {
                        log("NEXT_CONTEST_NOTICE_EXPIRED", notice->key);
                    } else {
                        log("NEXT_CONTEST_STATE_FAILED", error);
                        retry(300);
                    }
                } else if (!confirmed.has_value() || confirmed->startTime <= now) {
                    log("NEXT_CONTEST_NOTICE_UNCONFIRMED", notice->key);
                    retry(300);
                } else if (!repository.refreshNextContestNotice(notice->key, *confirmed, error)) {
                    log("NEXT_CONTEST_STATE_FAILED", error);
                    retry(300);
                } else {
                    std::string sendError;
                    std::string discordMessageId;
                    const SendResult result = discord.send(
                        makeNextContestContent(*confirmed), discordMessageId, sendError, false);
                    if (result == SendResult::Sent) {
                        if (repository.markNextContestNoticeSent(notice->key, discordMessageId, error)) {
                            log("NEXT_CONTEST_NOTICE_SENT", notice->key);
                        } else {
                            log("NEXT_CONTEST_STATE_FAILED", error);
                            retry(300);
                        }
                    } else {
                        log("NEXT_CONTEST_NOTICE_RETRY", sendError);
                        retry(result == SendResult::ConfigurationError ? 1800 : 300);
                    }
                }
            }
        }

        if (allowNotificationAction && state.has_value() && now < state->contest.startTime
            && now >= nextNotificationAction) {
            NotificationKind kind = NotificationKind::Normal;
            bool ready = false;
            if (pendingKind(*state).has_value()) {
                kind = *pendingKind(*state);
                ready = true;
            } else if (state->changeStatus == NotificationStatus::Pending
                && state->normalStatus == NotificationStatus::Sent) {
                kind = NotificationKind::ScheduleChanged;
                ready = true;
            } else if (shouldSendNormal(*state, now)) {
                ready = true;
            }

            if (ready) {
                const std::string key = notificationKey(kind, state->contest);
                state->pendingKind = kind;
                state->pendingMessageKey = key;
                state->updatedAt = now;
                if (!repository.markPending(kind, key, *state, error)) {
                    log("STATE_SAVE_FAILED", error);
                    nextNotificationAction = now + 300;
                } else {
                    std::string sendError;
                    std::string discordMessageId;
                    const SendResult result = discord.send(
                        makeNotificationContent(state->contest, config.discordRoleId),
                        discordMessageId, sendError);
                    if (result == SendResult::Sent) {
                        CurrentContest sentState = *state;
                        markSentState(sentState, kind);
                        if (!repository.markSent(key, discordMessageId, sentState, error)) {
                            log("STATE_SAVE_FAILED", error);
                            nextNotificationAction = now + 300;
                        } else {
                            state = std::move(sentState);
                            log("DISCORD_SEND_OK", key);
                            nextNotificationAction = 0;
                        }
                    } else {
                        log("DISCORD_SEND_RETRY", sendError);
                        nextNotificationAction = now + (result == SendResult::ConfigurationError ? 1800 : 300);
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(config.pollSeconds));
    }
}

int main() {
    try {
        return run();
    } catch (const std::exception& exception) {
        log("UNHANDLED_EXCEPTION", exception.what());
        return 1;
    } catch (...) {
        log("UNHANDLED_EXCEPTION", "unknown exception");
        return 1;
    }
}
