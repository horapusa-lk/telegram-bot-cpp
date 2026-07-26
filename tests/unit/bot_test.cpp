#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/bot.hpp>
#include <tgbot/keyboards.hpp>

#include "mock_http_client.hpp"

using nlohmann::json;
using tgbot::Bot;
using tgbot::Update;

namespace {

Bot make_bot() {
    tgbot::ApiClient::Options options;
    options.http = std::make_shared<tgbot::test::MockHttpClient>();
    return Bot(tgbot::Api(tgbot::ApiClient("TEST_TOKEN", options)));
}

Update message_update(const std::string& text) {
    return json{{"update_id", 1},
                {"message",
                 {{"message_id", 2},
                  {"date", 7},
                  {"chat", {{"id", 5}, {"type", "private"}}},
                  {"text", text}}}}
        .get<Update>();
}

}  // namespace

TEST_CASE("commands route by name, case-insensitively, with @mention and args", "[bot]") {
    Bot bot = make_bot();
    std::vector<std::string> hits;
    bot.onCommand("start", [&](const tgbot::Message& m) { hits.push_back(*m.text); });
    bot.onCommand("/help", [&](const tgbot::Message&) { hits.push_back("help"); });

    bot.dispatcher().dispatch(message_update("/start"));
    bot.dispatcher().dispatch(message_update("/START arg1 arg2"));
    bot.dispatcher().dispatch(message_update("/start@SomeBot payload"));
    bot.dispatcher().dispatch(message_update("/help"));
    bot.dispatcher().dispatch(message_update("/unknown"));

    CHECK(hits ==
          std::vector<std::string>{"/start", "/START arg1 arg2", "/start@SomeBot payload", "help"});
}

TEST_CASE("command messages are consumed; others reach onMessage", "[bot]") {
    Bot bot = make_bot();
    std::vector<std::string> plain;
    bool command_hit = false;
    bot.onCommand("start", [&](const tgbot::Message&) { command_hit = true; });
    bot.onMessage([&](const tgbot::Message& m) { plain.push_back(*m.text); });

    bot.dispatcher().dispatch(message_update("/start"));
    bot.dispatcher().dispatch(message_update("hello"));
    bot.dispatcher().dispatch(message_update("/unknown cmd"));  // no handler: falls through

    CHECK(command_hit);
    CHECK(plain == std::vector<std::string>{"hello", "/unknown cmd"});
}

TEST_CASE("callback and inline queries route to their handlers", "[bot]") {
    Bot bot = make_bot();
    std::string callback_data;
    std::string inline_query;
    bot.onCallbackQuery(
        [&](const tgbot::CallbackQuery& q) { callback_data = q.data.value_or(""); });
    bot.onInlineQuery([&](const tgbot::InlineQuery& q) { inline_query = q.query; });

    bot.dispatcher().dispatch(json{
        {"update_id", 1},
        {"callback_query",
         {{"id", "cbq"},
          {"from", {{"id", 9}, {"is_bot", false}, {"first_name", "u"}}},
          {"chat_instance", "ci"},
          {"data", "vote:yes"}}}}.get<Update>());
    bot.dispatcher().dispatch(json{
        {"update_id", 2},
        {"inline_query",
         {{"id", "iq"},
          {"from", {{"id", 9}, {"is_bot", false}, {"first_name", "u"}}},
          {"query", "cats"},
          {"offset", ""}}}}.get<Update>());

    CHECK(callback_data == "vote:yes");
    CHECK(inline_query == "cats");
}

TEST_CASE("onAnyUpdate sees everything", "[bot]") {
    Bot bot = make_bot();
    int count = 0;
    bot.onAnyUpdate([&](const Update&) { ++count; });
    bot.dispatcher().dispatch(message_update("/start"));
    bot.dispatcher().dispatch(json{{"update_id", 3}}.get<Update>());
    CHECK(count == 2);
}

TEST_CASE("InlineKeyboardBuilder lays out rows", "[keyboards]") {
    const auto markup = tgbot::InlineKeyboardBuilder()
                            .text("Yes", "vote:yes")
                            .text("No", "vote:no")
                            .row()
                            .url("Docs", "https://example.com")
                            .row()  // trailing empty row is dropped
                            .build();

    const json j = markup;
    CHECK(j == json{{"inline_keyboard",
                     {{{{"text", "Yes"}, {"callback_data", "vote:yes"}},
                       {{"text", "No"}, {"callback_data", "vote:no"}}},
                      {{{"text", "Docs"}, {"url", "https://example.com"}}}}}});
}

TEST_CASE("ReplyKeyboardBuilder covers options", "[keyboards]") {
    const auto markup = tgbot::ReplyKeyboardBuilder()
                            .text("Help")
                            .row()
                            .requestLocation("Where am I")
                            .resize()
                            .oneTime()
                            .placeholder("pick one")
                            .build();

    const json j = markup;
    CHECK(j["keyboard"][0][0] == json{{"text", "Help"}});
    CHECK(j["keyboard"][1][0] == json{{"text", "Where am I"}, {"request_location", true}});
    CHECK(j["resize_keyboard"] == true);
    CHECK(j["one_time_keyboard"] == true);
    CHECK(j["input_field_placeholder"] == "pick one");
    CHECK_FALSE(j.contains("is_persistent"));
}
