#include "atcoder_reminder/atcoder_client.hpp"
#include "atcoder_reminder/config.hpp"
#include "atcoder_reminder/discord_client.hpp"
#include "atcoder_reminder/message_template.hpp"
#include "atcoder_reminder/reminder_policy.hpp"
#include "atcoder_reminder/state_repository.hpp"
#include "atcoder_reminder/time_service.hpp"

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
        return state.contest.startTime <= now;
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
    config.discordToken = environment("ATCODER_DISCORD_BOT_TOKEN");
    config.discordChannelId = environment("ATCODER_DISCORD_CHANNEL_ID");
    config.discordRoleId = environment("ATCODER_DISCORD_ROLE_ID");
    const std::string configuredDb = environment("ATCODER_STATE_DB");
    if (!configuredDb.empty()) config.stateDbPath = configuredDb;

    if (config.discordToken.empty() || config.discordChannelId.empty()
        || config.discordRoleId.empty()) {
        log("CONFIG_ERROR", "ATCODER_DISCORD_BOT_TOKEN, ATCODER_DISCORD_CHANNEL_ID, and ATCODER_DISCORD_ROLE_ID are required");
        return 2;
    }

    CurlHttpClient http(config);
    AtCoderClient atcoder(http, config);
    RestDiscordClient discord(http, config);
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
    std::int64_t fetchFailures = 0;

    while (true) {
        const UnixSeconds now = currentUnixSeconds();
        bool allowNotificationAction = true;
        if (!isPlausibleUnixTime(now)) {
            log("TIME_INVALID");
            std::this_thread::sleep_for(std::chrono::minutes(1));
            continue;
        }

        if (state.has_value() && pendingKind(*state).has_value()
            && now >= nextNotificationAction) {
            const std::string key = state->pendingMessageKey.empty()
                ? notificationKey(*pendingKind(*state), state->contest)
                : state->pendingMessageKey;
            std::string reconcileError;
            if (discord.findByKey(key, reconcileError)) {
                const NotificationKind kind = *pendingKind(*state);
                CurrentContest sentState = *state;
                markSentState(sentState, kind);
                if (!repository.markSent(key, {}, sentState, error)) {
                    log("STATE_SAVE_FAILED", error);
                    nextNotificationAction = now + 300;
                } else {
                    state = std::move(sentState);
                    log("DISCORD_RECONCILED", key);
                    nextNotificationAction = 0;
                }
            } else if (!reconcileError.empty()) {
                log("DISCORD_RECONCILE_FAILED", reconcileError);
                nextNotificationAction = now + 300;
            } else {
                // No message was found. Keep the pending state and send it below.
                nextNotificationAction = now;
            }
        }

        if (now >= nextFetch) {
            const ParseResult fetched = atcoder.fetch(now);
            // An empty result is not treated as a valid schedule. Keeping an
            // old future contest after a parser/site change could cause a
            // notification for a stale time, so retry and do not send in this
            // iteration until a complete ABC candidate is available again.
            if (!fetched.tableFound || fetched.contests.empty()) {
                allowNotificationAction = false;
                ++fetchFailures;
                const std::int64_t backoff = std::min<std::int64_t>(3600, 300LL << std::min<std::int64_t>(fetchFailures - 1, 3));
                nextFetch = now + backoff;
                log("ATCODER_FETCH_FAILED",
                    fetched.error.empty() ? "no complete future ABC candidate" : fetched.error);
            } else {
                fetchFailures = 0;
                nextFetch = now + 60 * 60;
                bool stateChanged = false;
                if (!state.has_value() && !fetched.contests.empty()) {
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
                    if (shouldMove && state->contest.startTime <= now) {
                        if (!fetched.contests.empty()) {
                            state = newState(fetched.contests.front(), now);
                            stateChanged = true;
                            log("CONTEST_FINISHED", "moved to " + state->contest.id);
                        } else {
                            if (!repository.clear(error)) {
                                log("STATE_CLEAR_FAILED", error);
                            } else {
                                state.reset();
                                stateChanged = false;
                                log("CONTEST_FINISHED", "no future ABC found");
                            }
                        }
                    } else {
                        stateChanged = shouldMove;
                    }
                    const std::int64_t interval = atcoderCheckIntervalSeconds(
                        state.has_value() ? &*state : nullptr, now);
                    nextFetch = now + interval;
                }
                if (stateChanged && state.has_value()) {
                    state->updatedAt = now;
                    if (!repository.save(*state, error)) log("STATE_SAVE_FAILED", error);
                }
            }
        }

        if (allowNotificationAction && state.has_value() && now >= nextNotificationAction) {
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
                    const SendResult result = discord.send(
                        makeNotificationContent(state->contest, config.discordRoleId), key, sendError);
                    if (result == SendResult::Sent || result == SendResult::AlreadyExists) {
                        CurrentContest sentState = *state;
                        markSentState(sentState, kind);
                        if (!repository.markSent(key, {}, sentState, error)) {
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

        if (state.has_value() && state->contest.startTime <= now) {
            nextFetch = 0;
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
