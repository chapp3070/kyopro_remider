#include "atcoder_reminder/discord_client.hpp"

#include <nlohmann/json.hpp>

#include <cctype>

namespace reminder {
namespace {

using Json = nlohmann::json;

bool isNumeric(const std::string& value) {
    if (value.empty()) return false;
    for (const unsigned char c : value) {
        if (std::isdigit(c) == 0) return false;
    }
    return true;
}

bool containsWhitespace(const std::string& value) {
    for (const unsigned char c : value) {
        if (std::isspace(c) != 0) return true;
    }
    return false;
}

bool isDiscordWebhookUrl(const std::string& value) {
    return !containsWhitespace(value)
        && value.rfind("https://discord.com/api/webhooks/", 0) == 0;
}

std::string withWaitForMessage(const std::string& webhookUrl) {
    if (webhookUrl.find("wait=") != std::string::npos) return webhookUrl;
    return webhookUrl + (webhookUrl.find('?') == std::string::npos ? "?wait=true"
                                                                    : "&wait=true");
}

} // namespace

WebhookDiscordClient::WebhookDiscordClient(HttpClient& http, const Config& config)
    : http_(http), config_(config) {}

SendResult WebhookDiscordClient::send(const std::string& content,
                                      std::string& discordMessageId,
                                      std::string& error,
                                      bool mentionRole) {
    discordMessageId.clear();
    if (!isDiscordWebhookUrl(config_.discordWebhookUrl)
        || (mentionRole && !isNumeric(config_.discordRoleId))) {
        error = "Discord webhook URL or role ID is missing/invalid";
        return SendResult::ConfigurationError;
    }

    const Json request = {
        {"content", content},
        {"allowed_mentions", {{"parse", Json::array()},
                               {"roles", mentionRole ? Json::array({config_.discordRoleId})
                                                      : Json::array()}}},
        {"username", "AtCoder ABC Reminder"},
    };
    const HttpResponse response = http_.postJson(
        withWaitForMessage(config_.discordWebhookUrl), request.dump());
    if (!response.transportSucceeded()) {
        error = "Discord request failed: " + response.error;
        return SendResult::RetryableError;
    }
    if (response.status >= 200 && response.status < 300) {
        try {
            if (!response.body.empty()) {
                const Json message = Json::parse(response.body);
                if (message.contains("id") && message["id"].is_string()) {
                    discordMessageId = message["id"].get<std::string>();
                }
            }
        } catch (const Json::exception&) {
            discordMessageId.clear();
        }
        return SendResult::Sent;
    }
    if (response.status == 429 || response.status >= 500) {
        error = "Discord temporary HTTP error: " + std::to_string(response.status);
        return SendResult::RetryableError;
    }
    error = "Discord configuration/request error: HTTP " + std::to_string(response.status);
    return SendResult::ConfigurationError;
}

} // namespace reminder
