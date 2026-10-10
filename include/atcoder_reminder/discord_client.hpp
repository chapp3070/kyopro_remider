#pragma once

#include "atcoder_reminder/config.hpp"
#include "atcoder_reminder/ports.hpp"

namespace reminder {

class WebhookDiscordClient final : public DiscordClient {
public:
    WebhookDiscordClient(HttpClient& http, const Config& config);

    SendResult send(const std::string& content,
                    std::string& discordMessageId,
                    std::string& error,
                    bool mentionRole = true) override;

private:
    HttpClient& http_;
    const Config& config_;
};

} // namespace reminder
