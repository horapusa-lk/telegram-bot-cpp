// Generated smoke pass over every Api method: each one is invoked with
// default params against the mock transport; the request must target the
// right endpoint and parse back into the typed result without throwing.

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/api.hpp>

#include "generated/method_smoke.hpp"
#include "mock_http_client.hpp"

using tgbot::test::MethodSmokeCase;
using tgbot::test::MockHttpClient;

TEST_CASE("every generated method builds a request and parses its result", "[api][coverage]") {
    for (const MethodSmokeCase& c : tgbot::test::method_corpus()) {
        INFO("method: " << c.name);
        auto mock = std::make_shared<MockHttpClient>();
        mock->enqueue(200, std::string(R"({"ok":true,"result":)") + c.result_json + "}");
        tgbot::ApiClient::Options options;
        options.http = mock;
        const tgbot::Api api(tgbot::ApiClient("TEST_TOKEN", options));

        REQUIRE_NOTHROW(c.invoke(api));

        REQUIRE(mock->requests.size() == 1);
        const auto& request = mock->requests[0];
        const std::string suffix = std::string("/") + c.name;
        REQUIRE(request.url.size() >= suffix.size());
        CHECK(request.url.compare(request.url.size() - suffix.size(), suffix.size(), suffix) == 0);
        if (request.parts.empty()) {
            CHECK_NOTHROW(nlohmann::json::parse(request.body));
        }
    }
}
