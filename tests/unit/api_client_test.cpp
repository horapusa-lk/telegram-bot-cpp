#include <chrono>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/api_client.hpp>
#include <tgbot/api_error.hpp>
#include <tgbot/input_file.hpp>

#include "mock_http_client.hpp"

using nlohmann::json;
using tgbot::ApiClient;
using tgbot::ApiError;
using tgbot::NetworkError;
using tgbot::test::MockHttpClient;

namespace {

ApiClient make_client(std::shared_ptr<MockHttpClient> mock, int max_attempts = 3) {
    ApiClient::Options options;
    options.http = std::move(mock);
    options.retry.max_attempts = max_attempts;
    options.retry.initial_backoff = std::chrono::milliseconds(1);
    return ApiClient("TEST_TOKEN", options);
}

}  // namespace

TEST_CASE("successful call returns the result payload", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(200, R"({"ok":true,"result":{"id":42,"is_bot":true}})");
    auto client = make_client(mock);

    const json result = client.call("getMe", json::object());

    REQUIRE(result["id"] == 42);
    REQUIRE(mock->requests.size() == 1);
    CHECK(mock->requests[0].url == "https://api.telegram.org/botTEST_TOKEN/getMe");
    CHECK(mock->requests[0].content_type == "application/json");
    CHECK(json::parse(mock->requests[0].body) == json::object());
}

TEST_CASE("API error maps to ApiError with envelope fields", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(400, R"({"ok":false,"error_code":400,
        "description":"Bad Request: chat not found"})");
    auto client = make_client(mock);

    try {
        client.call("sendMessage", {{"chat_id", 1}, {"text", "hi"}});
        FAIL("expected ApiError");
    } catch (const ApiError& e) {
        CHECK(e.error_code() == 400);
        CHECK(e.description() == "Bad Request: chat not found");
        CHECK_FALSE(e.retry_after().has_value());
        CHECK_FALSE(e.migrate_to_chat_id().has_value());
        CHECK(std::string(e.what()).find("400") != std::string::npos);
    }
}

TEST_CASE("migrate_to_chat_id is surfaced from parameters", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(400, R"({"ok":false,"error_code":400,"description":"group upgraded",
        "parameters":{"migrate_to_chat_id":-100123456789}})");
    auto client = make_client(mock, 1);

    try {
        client.call("sendMessage", {{"chat_id", 1}, {"text", "hi"}});
        FAIL("expected ApiError");
    } catch (const ApiError& e) {
        REQUIRE(e.migrate_to_chat_id().has_value());
        CHECK(*e.migrate_to_chat_id() == -100123456789LL);
    }
}

TEST_CASE("429 with retry_after is honored and then succeeds", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(429, R"({"ok":false,"error_code":429,"description":"Too Many Requests",
        "parameters":{"retry_after":0}})");
    mock->enqueue(200, R"({"ok":true,"result":"done"})");
    auto client = make_client(mock);

    const json result = client.call("sendMessage", {{"chat_id", 1}, {"text", "hi"}});

    CHECK(result == "done");
    CHECK(mock->requests.size() == 2);
}

TEST_CASE("429 beyond the retry budget throws with retry_after set", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(429, R"({"ok":false,"error_code":429,"description":"Too Many Requests",
        "parameters":{"retry_after":0}})");
    auto client = make_client(mock, 1);  // single attempt: no retry possible

    try {
        client.call("sendMessage", {{"chat_id", 1}, {"text", "hi"}});
        FAIL("expected ApiError");
    } catch (const ApiError& e) {
        CHECK(e.error_code() == 429);
        REQUIRE(e.retry_after().has_value());
        CHECK(*e.retry_after() == 0);
    }
}

TEST_CASE("rate-limit waits longer than max_retry_after are not slept", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(429, R"({"ok":false,"error_code":429,"description":"Too Many Requests",
        "parameters":{"retry_after":99999}})");
    auto client = make_client(mock);

    CHECK_THROWS_AS(client.call("sendMessage", {{"chat_id", 1}, {"text", "hi"}}), ApiError);
    CHECK(mock->requests.size() == 1);  // gave up immediately, no retry sleep
}

TEST_CASE("transient network errors are retried with backoff", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue_network_error();
    mock->enqueue_network_error();
    mock->enqueue(200, R"({"ok":true,"result":123})");
    auto client = make_client(mock);

    CHECK(client.call("getMe", json::object()) == 123);
    CHECK(mock->requests.size() == 3);
}

TEST_CASE("network errors beyond the budget propagate", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue_network_error();
    auto client = make_client(mock, 2);

    CHECK_THROWS_AS(client.call("getMe", json::object()), NetworkError);
    CHECK(mock->requests.size() == 2);
}

TEST_CASE("5xx and non-envelope responses are retried", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(502, "<html>Bad Gateway</html>");
    mock->enqueue(200, R"({"ok":true,"result":true})");
    auto client = make_client(mock);

    CHECK(client.call("getMe", json::object()) == true);
    CHECK(mock->requests.size() == 2);
}

TEST_CASE("files promote the request to multipart with attach references", "[api_client]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(200, R"({"ok":true,"result":true})");
    auto client = make_client(mock);

    auto file = tgbot::InputFile::fromBuffer("PNGDATA", "cat.png", "image/png");
    const json params = {{"chat_id", 7}, {"photo", file.reference()}};
    client.call("sendPhoto", params, {file.to_part()});

    REQUIRE(mock->requests.size() == 1);
    const auto& parts = mock->requests[0].parts;
    REQUIRE(parts.size() == 3);  // chat_id, photo, file part

    // Form fields carry stringified values; the file part carries the bytes.
    bool saw_chat_id = false;
    bool saw_photo_ref = false;
    bool saw_file = false;
    for (const auto& part : parts) {
        if (part.name == "chat_id") {
            saw_chat_id = std::get<std::string>(part.data) == "7";
        } else if (part.name == "photo") {
            saw_photo_ref = std::get<std::string>(part.data) == file.reference();
        } else {
            saw_file = std::get<std::string>(part.data) == "PNGDATA" &&
                       part.filename == "cat.png" && part.content_type == "image/png" &&
                       ("attach://" + part.name) == file.reference();
        }
    }
    CHECK(saw_chat_id);
    CHECK(saw_photo_ref);
    CHECK(saw_file);
}
