// Request-construction tests: exact JSON bodies, multipart promotion,
// attach:// wiring for nested media, long-poll timeout handling, and typed
// result parsing including the MessageOrBool union.

#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/api.hpp>

#include "mock_http_client.hpp"

using nlohmann::json;
using tgbot::Api;
using tgbot::ApiClient;
using tgbot::InputFile;
using tgbot::test::MockHttpClient;

namespace {

struct Fixture {
    std::shared_ptr<MockHttpClient> mock = std::make_shared<MockHttpClient>();
    Api api;

    Fixture() : api(make()) {}

    ApiClient make() {
        ApiClient::Options options;
        options.http = mock;
        return ApiClient("TEST_TOKEN", options);
    }
};

const char* kMinimalMessage =
    R"({"ok":true,"result":{"message_id":1,"date":7,"chat":{"id":5,"type":"private"}}})";

}  // namespace

TEST_CASE("sendMessage builds the documented JSON body", "[api]") {
    Fixture f;
    f.mock->enqueue(200, kMinimalMessage);

    tgbot::Message sent = f.api.sendMessage({
        .chat_id = std::int64_t{5},
        .text = "hello",
        .parse_mode = "MarkdownV2",
        .disable_notification = true,
    });

    CHECK(sent.message_id == 1);
    REQUIRE(f.mock->requests.size() == 1);
    const auto& req = f.mock->requests[0];
    CHECK(req.url == "https://api.telegram.org/botTEST_TOKEN/sendMessage");
    const json body = json::parse(req.body);
    CHECK(body == json{{"chat_id", 5},
                       {"text", "hello"},
                       {"parse_mode", "MarkdownV2"},
                       {"disable_notification", true}});
}

TEST_CASE("string chat ids serialize as strings", "[api]") {
    Fixture f;
    f.mock->enqueue(200, kMinimalMessage);

    (void)f.api.sendMessage({.chat_id = std::string("@channel"), .text = "hi"});

    const json body = json::parse(f.mock->requests[0].body);
    CHECK(body["chat_id"] == "@channel");
}

TEST_CASE("sendPhoto with a buffer upload becomes multipart", "[api]") {
    Fixture f;
    f.mock->enqueue(200, kMinimalMessage);

    auto photo = InputFile::fromBuffer("JPEGDATA", "cat.jpg", "image/jpeg");
    (void)f.api.sendPhoto({.chat_id = std::int64_t{5}, .photo = photo});

    const auto& req = f.mock->requests[0];
    REQUIRE_FALSE(req.parts.empty());

    std::string photo_field;
    std::string upload_name;
    std::string upload_bytes;
    for (const auto& part : req.parts) {
        if (part.name == "photo") {
            photo_field = std::get<std::string>(part.data);
        } else if (part.name != "chat_id") {
            upload_name = part.name;
            upload_bytes = std::get<std::string>(part.data);
        }
    }
    CHECK(photo_field == photo.reference());
    CHECK("attach://" + upload_name == photo.reference());
    CHECK(upload_bytes == "JPEGDATA");
}

TEST_CASE("sendPhoto by file_id stays a JSON request", "[api]") {
    Fixture f;
    f.mock->enqueue(200, kMinimalMessage);

    (void)f.api.sendPhoto(
        {.chat_id = std::int64_t{5}, .photo = InputFile::fromFileId("AgACAg123")});

    const auto& req = f.mock->requests[0];
    CHECK(req.parts.empty());
    CHECK(json::parse(req.body)["photo"] == "AgACAg123");
}

TEST_CASE("sendMediaGroup wires nested attach:// uploads", "[api]") {
    Fixture f;
    f.mock->enqueue(200, R"({"ok":true,"result":[]})");

    tgbot::InputMediaPhoto uploaded;
    uploaded.type = "photo";
    uploaded.media = InputFile::fromBuffer("PNG1", "a.png", "image/png");
    tgbot::InputMediaPhoto by_url;
    by_url.type = "photo";
    by_url.media = InputFile::fromUrl("https://example.com/b.png");

    (void)f.api.sendMediaGroup({
        .chat_id = std::int64_t{5},
        .media = {tgbot::InputMedia{uploaded}, tgbot::InputMedia{by_url}},
    });

    const auto& req = f.mock->requests[0];
    REQUIRE_FALSE(req.parts.empty());

    json media;
    int uploads = 0;
    for (const auto& part : req.parts) {
        if (part.name == "media") {
            media = json::parse(std::get<std::string>(part.data));
        } else if (part.name != "chat_id") {
            ++uploads;
            CHECK(std::get<std::string>(part.data) == "PNG1");
        }
    }
    REQUIRE(uploads == 1);
    REQUIRE(media.is_array());
    CHECK(media[0]["media"] == uploaded.media.reference());
    CHECK(media[1]["media"] == "https://example.com/b.png");
}

TEST_CASE("getUpdates extends the transport timeout beyond the poll", "[api]") {
    Fixture f;
    f.mock->enqueue(200, R"({"ok":true,"result":[]})");

    (void)f.api.getUpdates({.offset = std::int64_t{42}, .timeout = std::int64_t{50}});

    const auto& req = f.mock->requests[0];
    const json body = json::parse(req.body);
    CHECK(body["offset"] == 42);
    CHECK(body["timeout"] == 50);
    CHECK(req.timeout == std::chrono::seconds(60));
}

TEST_CASE("editMessageText parses both arms of MessageOrBool", "[api]") {
    Fixture f;
    f.mock->enqueue(200, R"({"ok":true,"result":true})");
    f.mock->enqueue(200, kMinimalMessage);

    tgbot::MessageOrBool inline_edit =
        f.api.editMessageText({.inline_message_id = "abc", .text = "new"});
    REQUIRE(std::holds_alternative<bool>(inline_edit));
    CHECK(std::get<bool>(inline_edit));

    tgbot::MessageOrBool chat_edit = f.api.editMessageText(
        {.chat_id = tgbot::ChatId{std::int64_t{5}}, .message_id = std::int64_t{1}, .text = "new"});
    REQUIRE(std::holds_alternative<tgbot::Message>(chat_edit));
    CHECK(std::get<tgbot::Message>(chat_edit).message_id == 1);
}

TEST_CASE("getMe parses the bot user", "[api]") {
    Fixture f;
    f.mock->enqueue(200, R"({"ok":true,"result":{"id":99,"is_bot":true,"first_name":"b"}})");

    const tgbot::User me = f.api.getMe();

    CHECK(me.id == 99);
    CHECK(me.is_bot);
    CHECK(f.mock->requests[0].url == "https://api.telegram.org/botTEST_TOKEN/getMe");
}
