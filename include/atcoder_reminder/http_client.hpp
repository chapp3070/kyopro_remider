#pragma once

#include "atcoder_reminder/config.hpp"
#include "atcoder_reminder/ports.hpp"

namespace reminder {

class CurlHttpClient final : public HttpClient {
public:
    explicit CurlHttpClient(const Config& config);
    ~CurlHttpClient() override;

    HttpResponse get(const std::string& url, std::size_t maxBytes) override;
    HttpResponse getJson(const std::string& url,
                         const std::string& authorization,
                         std::size_t maxBytes) override;
    HttpResponse postJson(const std::string& url,
                          const std::string& authorization,
                          const std::string& body) override;

private:
    const Config& config_;
};

} // namespace reminder
