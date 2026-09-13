#include "atcoder_reminder/atcoder_client.hpp"

namespace reminder {

AtCoderClient::AtCoderClient(HttpClient& http, const Config& config)
    : http_(http), config_(config) {}

ParseResult AtCoderClient::fetch(UnixSeconds now) {
    const HttpResponse response = http_.get(config_.atcoderUrl, config_.atcoderResponseLimit);
    if (!response.transportSucceeded()) {
        return ParseResult{false, {}, "AtCoder request failed: " + response.error};
    }
    if (response.status < 200 || response.status >= 300) {
        return ParseResult{false, {}, "AtCoder returned HTTP " + std::to_string(response.status)};
    }
    return parser_.parse(response.body, now);
}

} // namespace reminder
