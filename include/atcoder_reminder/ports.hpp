#pragma once

#include "atcoder_reminder/domain.hpp"

#include <optional>
#include <string>

namespace reminder {

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const std::string& url, std::size_t maxBytes) = 0;
    virtual HttpResponse getJson(const std::string& url,
                                 const std::string& authorization,
                                 std::size_t maxBytes) = 0;
    virtual HttpResponse postJson(const std::string& url,
                                  const std::string& authorization,
                                  const std::string& body) = 0;
};

class StateRepository {
public:
    virtual ~StateRepository() = default;
    virtual bool initialize(std::string& error) = 0;
    virtual std::optional<CurrentContest> load(std::string& error) = 0;
    virtual bool save(const CurrentContest& state, std::string& error) = 0;
    virtual bool clear(std::string& error) = 0;
    virtual bool markPending(NotificationKind kind,
                             const std::string& key,
                             const CurrentContest& state,
                             std::string& error) = 0;
    virtual bool markSent(const std::string& key,
                          const std::string& discordMessageId,
                          const CurrentContest& state,
                          std::string& error) = 0;
};

class DiscordClient {
public:
    virtual ~DiscordClient() = default;
    virtual SendResult send(const std::string& content,
                            const std::string& messageKey,
                            std::string& error) = 0;
    virtual bool findByKey(const std::string& messageKey,
                           std::string& error) = 0;
};

} // namespace reminder
