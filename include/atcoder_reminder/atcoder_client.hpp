#pragma once

#include "atcoder_reminder/atcoder_html_parser.hpp"
#include "atcoder_reminder/http_client.hpp"

namespace reminder {

class AtCoderClient {
public:
    AtCoderClient(HttpClient& http, const Config& config);
    ParseResult fetch(UnixSeconds now);

private:
    HttpClient& http_;
    const Config& config_;
    AtCoderHtmlParser parser_;
};

} // namespace reminder
