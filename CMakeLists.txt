cmake_minimum_required(VERSION 3.16)

project(atcoder_abc_reminder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)
find_package(SQLite3 REQUIRED)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)

add_library(reminder_core
    src/atcoder_html_parser.cpp
    src/atcoder_client.cpp
    src/discord_client.cpp
    src/http_client.cpp
    src/message_template.cpp
    src/reminder_policy.cpp
    src/state_repository.cpp
    src/time_service.cpp
)

target_include_directories(reminder_core
    PUBLIC include
    PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR}
)
target_link_libraries(reminder_core
    PUBLIC PkgConfig::CURL
           PkgConfig::LIBXML2
           SQLite::SQLite3
)
target_compile_options(reminder_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

add_executable(atcoder-abc-reminder src/main.cpp)
target_link_libraries(atcoder-abc-reminder PRIVATE reminder_core)

enable_testing()
add_executable(reminder_tests tests/test_main.cpp)
target_link_libraries(reminder_tests PRIVATE reminder_core)
add_test(NAME reminder_tests COMMAND reminder_tests)
