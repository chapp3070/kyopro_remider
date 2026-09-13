#pragma once

#include "atcoder_reminder/domain.hpp"

namespace reminder {

class AtCoderHtmlParser {
public:
    ParseResult parse(const std::string& html, UnixSeconds now) const;
};

} // namespace reminder
