#include <catch2/catch_test_macros.hpp>
#include <tgbot/api_error.hpp>

using tgbot::ApiError;
using tgbot::Error;
using tgbot::NetworkError;

TEST_CASE("ApiError carries code, description and parameters", "[error]") {
    ApiError e(429, "Too Many Requests: retry later", 35, std::nullopt);
    CHECK(e.error_code() == 429);
    CHECK(e.description() == "Too Many Requests: retry later");
    REQUIRE(e.retry_after().has_value());
    CHECK(*e.retry_after() == 35);
    CHECK_FALSE(e.migrate_to_chat_id().has_value());
    CHECK(std::string(e.what()) == "Telegram Bot API error 429: Too Many Requests: retry later");
}

TEST_CASE("exception hierarchy is catchable at every level", "[error]") {
    CHECK_THROWS_AS([] { throw ApiError(400, "bad"); }(), Error);
    CHECK_THROWS_AS([] { throw NetworkError("down"); }(), Error);
    CHECK_THROWS_AS([] { throw ApiError(400, "bad"); }(), std::runtime_error);
}
