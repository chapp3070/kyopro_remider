#include "atcoder_reminder/discord_client.hpp"

#include <nlohmann/json.hpp>

#include <cctype>

namespace reminder {
namespace {

using Json = nlohmann::json;

std::string channelIdUrl(const Config& config) {
    return "https://discord.com/api/v10/channels/" + config.discordChannelId + "/messages";
}

bool isNumeric(const std::string& value) {
    if (value.empty()) return false;
    for (const unsigned char c : value) {
        if (std::isdigit(c) == 0) return false;
    }
    return true;
}

} // namespace

RestDiscordClient::RestDiscordClient(HttpClient& http, const Config& config)
    : http_(http), config_(config) {}

SendResult RestDiscordClient::send(const std::string& content,
                                   const std::string& messageKey,
                                   std::string& error) {
    if (config_.discordToken.empty() || !isNumeric(config_.discordChannelId)
        || !isNumeric(config_.discordRoleId)) {
        error = "Discord token, channel ID, or role ID is missing/invalid";
        return SendResult::ConfigurationError;
    }

    const Json request = {
        {"content", content},
        {"allowed_mentions", {{"parse", Json::array()},
                               {"roles", Json::array({config_.discordRoleId})}}},
        {"nonce", messageKey},
        {"enforce_nonce", true},
    };
    const HttpResponse response = http_.postJson(
        channelIdUrl(config_), "Bot " + config_.discordToken, request.dump());
    if (!response.transportSucceeded()) {
        error = "Discord request failed: " + response.error;
        return SendResult::RetryableError;
    }
    if (response.status >= 200 && response.status < 300) {
        return SendResult::Sent;
    }
    if (response.status == 429 || response.status >= 500) {
        error = "Discord temporary HTTP error: " + std::to_string(response.status);
        return SendResult::RetryableError;
    }
    error = "Discord configuration/request error: HTTP " + std::to_string(response.status);
    return SendResult::ConfigurationError;
}

bool RestDiscordClient::findByKey(const std::string& messageKey, std::string& error) {
    if (config_.discordToken.empty() || !isNumeric(config_.discordChannelId)
        || !isNumeric(config_.discordRoleId)) {
        error = "Discord token, channel ID, or role ID is missing/invalid";
        return false;
    }

    const HttpResponse response = http_.getJson(
        channelIdUrl(config_) + "?limit=100", "Bot " + config_.discordToken,
        512U * 1024U);
    if (!response.transportSucceeded()) {
        error = "Discord history request failed: " + response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        error = "Discord history HTTP error: " + std::to_string(response.status);
        return false;
    }

    try {
        const Json messages = Json::parse(response.body);
        if (!messages.is_array()) {
            error = "Discord history response was not an array";
            return false;
        }
        for (const Json& message : messages) {
            if (message.contains("nonce") && message["nonce"].is_string()
                && message["nonce"].get<std::string>() == messageKey) {
                return true;
            }
        }
    } catch (const Json::exception& exception) {
        error = std::string("could not parse Discord history: ") + exception.what();
        return false;
    }
    return false;
}

} // namespace reminder
