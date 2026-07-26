#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/dispatcher.hpp>

using nlohmann::json;
using tgbot::Dispatcher;
using tgbot::Update;

TEST_CASE("handlers run in registration order", "[dispatcher]") {
    Dispatcher d;
    std::vector<int> order;
    d.onUpdate([&](const Update&) { order.push_back(1); });
    d.onUpdate([&](const Update&) { order.push_back(2); });

    Update u;
    u.update_id = 7;
    d.dispatch(u);

    CHECK(order == std::vector<int>{1, 2});
}

TEST_CASE("a throwing handler reaches the error hook, later handlers still run", "[dispatcher]") {
    Dispatcher d;
    bool second_ran = false;
    std::string message;
    d.onUpdate([](const Update&) { throw std::runtime_error("boom"); });
    d.onUpdate([&](const Update&) { second_ran = true; });
    d.onError([&](std::exception_ptr e) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            message = ex.what();
        }
    });

    d.dispatch(Update{});

    CHECK(second_ran);
    CHECK(message == "boom");
}

TEST_CASE("a throwing handler without an error hook is swallowed", "[dispatcher]") {
    Dispatcher d;
    d.onUpdate([](const Update&) { throw std::runtime_error("boom"); });
    CHECK_NOTHROW(d.dispatch(Update{}));
}

TEST_CASE("dispatch_json parses the payload into an Update", "[dispatcher]") {
    Dispatcher d;
    std::int64_t seen = 0;
    d.onUpdate([&](const Update& u) { seen = u.update_id; });

    d.dispatch_json(json::parse(R"({"update_id":42})"));
    CHECK(seen == 42);

    CHECK_THROWS(d.dispatch_json(json::parse(R"({"no_update_id":true})")));
}

TEST_CASE("dispatch_error feeds the hook directly", "[dispatcher]") {
    Dispatcher d;
    bool hit = false;
    d.onError([&](std::exception_ptr) { hit = true; });
    d.dispatch_error(std::make_exception_ptr(std::runtime_error("x")));
    CHECK(hit);
}
