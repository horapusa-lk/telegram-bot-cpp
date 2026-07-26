// Live integration tests against the real Bot API.
//
// Credentials come exclusively from the environment (loaded from .env by
// main.cpp): TG_BOT_TOKEN, TG_WEBHOOK_URL, TG_WEBHOOK_BIND, optionally
// TG_TEST_CHAT_ID.  The three phases run in declaration order inside one
// process because an active webhook blocks getUpdates:
//   1. getMe
//   2. sendMessage round-trip via polling (chat discovered from pending
//      updates, else TG_TEST_CHAT_ID; skipped when neither exists)
//   3. full webhook flow through the public tunnel endpoint: bind
//      TG_WEBHOOK_BIND, setWebhook(TG_WEBHOOK_URL, secret_token), push an
//      update through the public URL, verify dispatch + 403 on a bad secret,
//      then deleteWebhook.

#ifdef _MSC_VER
#pragma warning(disable : 4996)  // std::getenv
#endif

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/tgbot.hpp>

namespace {

std::string require_env(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') {
        FAIL("environment variable " << name << " is required for integration tests");
    }
    return v;
}

tgbot::Api make_api() {
    return tgbot::Api(require_env("TG_BOT_TOKEN"));
}

constexpr const char* kSecretHeader = "X-Telegram-Bot-Api-Secret-Token";

tgbot::HttpResponse post_update(const std::string& url, const nlohmann::json& update,
                                const std::string& secret) {
    tgbot::CurlHttpClient client;
    tgbot::HttpRequest request;
    request.url = url;
    request.body = update.dump();
    request.content_type = "application/json";
    if (!secret.empty()) {
        request.headers.emplace_back(kSecretHeader, secret);
    }
    return client.send(request);
}

}  // namespace

TEST_CASE("live: getMe identifies the bot", "[integration]") {
    const tgbot::Api api = make_api();
    const tgbot::User me = api.getMe();
    INFO("bot id " << me.id << " username @" << me.username.value_or("?"));
    CHECK(me.is_bot);
    CHECK(me.id > 0);
    WARN("getMe OK: @" << me.username.value_or("?") << " (id " << me.id << ")");
}

TEST_CASE("live: sendMessage round-trip via polling", "[integration]") {
    const tgbot::Api api = make_api();
    // Make sure polling is possible even if an earlier run left a webhook.
    api.deleteWebhook({});

    std::optional<std::int64_t> chat_id;
    if (const char* explicit_chat = std::getenv("TG_TEST_CHAT_ID");
        explicit_chat != nullptr && *explicit_chat != '\0') {
        chat_id = std::stoll(explicit_chat);
    } else {
        // Discover a chat from pending updates (someone messaged the bot).
        for (const tgbot::Update& u : api.getUpdates({.timeout = std::int64_t{0}})) {
            if (u.message.has_value()) {
                chat_id = u.message->chat.id;
            }
        }
    }
    if (!chat_id.has_value()) {
        WARN(
            "sendMessage round-trip SKIPPED: no chat available. Message the bot "
            "once or set TG_TEST_CHAT_ID to enable this test.");
        return;
    }

    const std::string text =
        "tgbot-cpp-full integration ping (api " + std::string(tgbot::api_version()) + ")";
    const tgbot::Message sent = api.sendMessage({.chat_id = *chat_id, .text = text});
    CHECK(sent.message_id > 0);
    REQUIRE(sent.text.has_value());
    CHECK(*sent.text == text);
    WARN("sendMessage OK: message_id " << sent.message_id << " in chat " << *chat_id);
}

TEST_CASE("live: full webhook flow through the tunnel", "[integration]") {
    const tgbot::Api api = make_api();
    const std::string public_url = require_env("TG_WEBHOOK_URL");
    const std::string bind = require_env("TG_WEBHOOK_BIND");
    const auto colon = bind.rfind(':');
    REQUIRE(colon != std::string::npos);
    const std::string host = bind.substr(0, colon);
    const int port = std::stoi(bind.substr(colon + 1));

    const std::string secret =
        "itest-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());

    // Local server on the tunnel-forwarded bind address.
    tgbot::Dispatcher dispatcher;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<std::int64_t> received;
    dispatcher.onUpdate([&](const tgbot::Update& u) {
        const std::lock_guard<std::mutex> lock(mutex);
        received = u.update_id;
        cv.notify_all();
    });
    tgbot::WebhookOptions options;
    options.host = host;
    options.port = port;
    options.secret_token = secret;
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());

    // Preflight: is the public endpoint reachable at all?  When the tunnel
    // hostname is not routed (no DNS record), fall back to the local bind so
    // the flow is still exercised end to end over real HTTP — and say so
    // loudly, because only the Cloudflare account owner can route it.
    std::string endpoint = public_url;
    bool public_path = true;
    try {
        (void)post_update(public_url, {{"update_id", 424240}}, "preflight");
    } catch (const tgbot::NetworkError& e) {
        public_path = false;
        endpoint = "http://" + bind + "/";
        WARN("PUBLIC ENDPOINT UNREACHABLE ("
             << e.what()
             << ") — the tunnel "
                "hostname is not routed. Falling back to the local bind "
             << endpoint
             << ". Route the hostname in Cloudflare Zero Trust and re-run for "
                "full coverage.");
    }

    // Register the public endpoint with Telegram (independent of our POSTs).
    bool webhook_registered = false;
    try {
        webhook_registered = api.setWebhook({.url = public_url, .secret_token = secret});
    } catch (const tgbot::ApiError& e) {
        if (public_path) {
            throw;  // endpoint reachable yet Telegram rejected it: a real bug
        }
        WARN("setWebhook rejected while the hostname is unrouted ("
             << e.what() << ") — Telegram-side assertions skipped.");
    }
    if (webhook_registered) {
        const tgbot::WebhookInfo info = api.getWebhookInfo();
        CHECK(info.url == public_url);
        WARN("setWebhook OK: " << info.url);
    }

    // A request with the wrong secret must be rejected by our server.
    const auto forbidden = post_update(endpoint, {{"update_id", 424241}}, "wrong-secret");
    CHECK(forbidden.status_code == 403);

    // Send ourselves an update through the deepest reachable path
    // (ideally internet -> Cloudflare TLS -> tunnel -> 127.0.0.1 bind).
    const std::int64_t marker = 424242;
    const auto accepted = post_update(endpoint, {{"update_id", marker}}, secret);
    CHECK(accepted.status_code == 200);

    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool got =
            cv.wait_for(lock, std::chrono::seconds(30), [&] { return received.has_value(); });
        REQUIRE(got);
        CHECK(*received == marker);
    }
    WARN("webhook update " << marker << " arrived via " << endpoint
                           << (public_path ? " (full tunnel path)" : " (local fallback path)"));

    // Clean up: back to a webhook-less state.
    if (webhook_registered) {
        CHECK(api.deleteWebhook({}));
        const tgbot::WebhookInfo after = api.getWebhookInfo();
        CHECK(after.url.empty());
        WARN("deleteWebhook OK");
    }
    server.stop();
}
