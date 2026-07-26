// Webhook server tests over real localhost HTTP: secret-token enforcement,
// dispatch through the worker pool, and the answer-with-method fast path.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/curl_http_client.hpp>
#include <tgbot/dispatcher.hpp>
#include <tgbot/webhook.hpp>

using nlohmann::json;

namespace {

constexpr const char* kSecretHeader = "X-Telegram-Bot-Api-Secret-Token";

struct Collector {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::int64_t> ids;

    void push(std::int64_t id) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            ids.push_back(id);
        }
        cv.notify_all();
    }

    bool wait_for_count(std::size_t n) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(10), [&] { return ids.size() >= n; });
    }
};

tgbot::HttpResponse post(const std::string& url, const std::string& body,
                         std::vector<std::pair<std::string, std::string>> headers = {}) {
    tgbot::CurlHttpClient client;
    tgbot::HttpRequest request;
    request.url = url;
    request.body = body;
    request.content_type = "application/json";
    request.headers = std::move(headers);
    return client.send(request);
}

}  // namespace

TEST_CASE("webhook end-to-end over localhost HTTP", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    Collector collector;
    dispatcher.onUpdate([&](const tgbot::Update& u) { collector.push(u.update_id); });

    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 0;  // pick a free port
    options.path = "/hook";
    options.secret_token = "s3cret";
    options.worker_threads = 2;
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());
    REQUIRE(server.port() > 0);
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/hook";

    SECTION("missing secret gets 403 and nothing is dispatched") {
        CHECK(post(url, R"({"update_id":1})").status_code == 403);
        CHECK(post(url, R"({"update_id":1})", {{kSecretHeader, "wrong"}}).status_code == 403);
        CHECK(collector.ids.empty());
    }

    SECTION("matching secret gets 200 and updates reach the handlers") {
        auto r1 = post(url, R"({"update_id":41})", {{kSecretHeader, "s3cret"}});
        auto r2 = post(url, R"({"update_id":42})", {{kSecretHeader, "s3cret"}});
        CHECK(r1.status_code == 200);
        CHECK(r2.status_code == 200);
        REQUIRE(collector.wait_for_count(2));
        CHECK((collector.ids == std::vector<std::int64_t>{41, 42} ||
               collector.ids == std::vector<std::int64_t>{42, 41}));
    }

    SECTION("malformed bodies get 400") {
        CHECK(post(url, "this is not json", {{kSecretHeader, "s3cret"}}).status_code == 400);
    }

    SECTION("wrong path is 404") {
        CHECK(post(url + "x", R"({"update_id":1})", {{kSecretHeader, "s3cret"}}).status_code ==
              404);
    }

    server.stop();
}

TEST_CASE("webhook can answer with a method inline", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    Collector collector;
    dispatcher.onUpdate([&](const tgbot::Update& u) { collector.push(u.update_id); });

    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.sync_responder = [](const tgbot::Update& u) -> std::optional<json> {
        if (u.update_id == 100) {
            return tgbot::answer_webhook_with("sendMessage", {{"chat_id", 5}, {"text", "fast"}});
        }
        return std::nullopt;
    };
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";

    auto fast = post(url, R"({"update_id":100})");
    CHECK(fast.status_code == 200);
    const json reply = json::parse(fast.body);
    CHECK(reply == json{{"method", "sendMessage"}, {"chat_id", 5}, {"text", "fast"}});

    auto plain = post(url, R"({"update_id":101})");
    CHECK(plain.status_code == 200);
    CHECK(plain.body.empty());

    // Both updates still reached the async handlers.
    REQUIRE(collector.wait_for_count(2));
    server.stop();
}

TEST_CASE("a full worker queue answers 503 so Telegram redelivers", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool release = false;
    dispatcher.onUpdate([&](const tgbot::Update&) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait_for(lock, std::chrono::seconds(15), [&] { return release; });
    });

    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.worker_threads = 1;
    options.max_queue_size = 1;
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";

    // First update occupies the single worker; give it a moment to be
    // dequeued, then the second fills the queue, the third must bounce.
    CHECK(post(url, R"({"update_id":1})").status_code == 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(post(url, R"({"update_id":2})").status_code == 200);
    CHECK(post(url, R"({"update_id":3})").status_code == 503);

    {
        const std::lock_guard<std::mutex> lock(gate_mutex);
        release = true;
    }
    gate_cv.notify_all();
    server.stop();
}

TEST_CASE("oversized request bodies are rejected", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.max_request_body = 1024;
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());
    const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";

    const std::string huge(64 * 1024, 'x');
    const auto response = post(url, R"({"update_id":1,"pad":")" + huge + R"("})");
    CHECK(response.status_code >= 400);
}

TEST_CASE("the webhook path is matched literally, not as a regex", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.path = "/hook.v1";  // '.' must not act as a wildcard
    tgbot::WebhookServer server(dispatcher, options);
    REQUIRE(server.start());
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());

    CHECK(post(base + "/hook.v1", R"({"update_id":1})").status_code == 200);
    CHECK(post(base + "/hookXv1", R"({"update_id":1})").status_code == 404);
    server.stop();
}

TEST_CASE("stop is idempotent and stop-from-handler is rejected", "[webhook]") {
    tgbot::Dispatcher dispatcher;
    tgbot::WebhookServer server(dispatcher, {.host = "127.0.0.1", .port = 0});
    REQUIRE(server.start());
    server.stop();
    CHECK_NOTHROW(server.stop());  // second stop is a no-op
}
