#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace reminder {

struct Config {
    std::string discordToken;
    std::string discordChannelId;
    std::string discordRoleId;
    std::string stateDbPath{"/srv/shared/remider/state.db"};
    std::string atcoderUrl{"https://atcoder.jp/contests/?lang=ja"};
    std::size_t atcoderResponseLimit{2U * 1024U * 1024U};
    std::int64_t pollSeconds{30};
    std::int64_t requestTimeoutSeconds{30};
};

} // namespace reminder
