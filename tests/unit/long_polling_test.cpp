#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/api.hpp>
#include <tgbot/dispatcher.hpp>
#include <tgbot/long_polling.hpp>

#include "mock_http_client.hpp"

using nlohmann::json;
using tgbot::test::MockHttpClient;

namespace {

std::string batch(std::initializer_list<int> ids) {
    json updates = json::array();
    for (int id : ids) {
        updates.push_back({{"update_id", id}});
    }
    return json{{"ok", true}, {"result", updates}}.dump();
}

}  // namespace

TEST_CASE("polling dispatches batches, advances offset, and survives errors", "[polling]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue(200, batch({1, 2}));
    mock->enqueue_network_error();
    mock->enqueue(200, batch({3}));
    mock->enqueue(200, batch({}));  // repeats forever

    tgbot::ApiClient::Options client_options;
    client_options.http = mock;
    client_options.retry.max_attempts = 1;  // let the poller's recovery act
    const tgbot::Api api(tgbot::ApiClient("TEST_TOKEN", client_options));

    tgbot::Dispatcher dispatcher;
    std::vector<std::int64_t> seen;
    tgbot::LongPollOptions options;
    options.timeout_seconds = 0;
    options.error_backoff = std::chrono::milliseconds(1);
    tgbot::LongPoller poller(api, dispatcher, options);

    dispatcher.onUpdate([&](const tgbot::Update& u) {
        seen.push_back(u.update_id);
        if (u.update_id == 3) {
            poller.stop();
        }
    });

    poller.run();  // exits via stop() inside the handler

    CHECK(seen == std::vector<std::int64_t>{1, 2, 3});
    CHECK(poller.offset() == 4);
    CHECK_FALSE(poller.running());

    // Offsets on the wire: first request has none, later requests ack.
    REQUIRE(mock->requests.size() >= 3);
    const json first = json::parse(mock->requests[0].body);
    CHECK_FALSE(first.contains("offset"));
    const json third = json::parse(mock->requests[2].body);
    CHECK(third["offset"] == 3);
}

TEST_CASE("stop() interrupts the error backoff promptly", "[polling]") {
    auto mock = std::make_shared<MockHttpClient>();
    mock->enqueue_network_error();

    tgbot::ApiClient::Options client_options;
    client_options.http = mock;
    client_options.retry.max_attempts = 1;
    const tgbot::Api api(tgbot::ApiClient("TEST_TOKEN", client_options));

    tgbot::Dispatcher dispatcher;
    tgbot::LongPollOptions options;
    options.timeout_seconds = 0;
    options.error_backoff = std::chrono::hours(1);  // must not actually wait
    tgbot::LongPoller poller(api, dispatcher, options);

    std::thread runner([&] { poller.run(); });
    while (!poller.running()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t0 = std::chrono::steady_clock::now();
    poller.stop();
    runner.join();
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    CHECK(elapsed < std::chrono::seconds(10));
}
