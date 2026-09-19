#pragma once

#include "atcoder_reminder/config.hpp"
#include "atcoder_reminder/ports.hpp"

namespace reminder {

class WebhookDiscordClient final : public DiscordClient {
public:
    WebhookDiscordClient(HttpClient& http, const Config& config);

    SendResult send(const std::string& content,
                    const std::string& messageKey,
                    std::string& discordMessageId,
                    std::string& error) override;
    bool findByKey(const std::string& messageKey,
                   std::string& error) override;

private:
    HttpClient& http_;
    const Config& config_;
};

} // namespace reminder
