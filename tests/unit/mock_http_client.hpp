#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <tgbot/api_error.hpp>
#include <tgbot/http_client.hpp>

namespace tgbot::test {

/// Scripted HttpClient: records every request and replays canned responses
/// (or throws) in order.  The last script entry repeats forever.
class MockHttpClient final : public HttpClient {
public:
    struct Step {
        HttpResponse response;
        bool throw_network_error = false;
    };

    void enqueue(int status, std::string body) {
        script_.push_back(Step{HttpResponse{status, std::move(body)}, false});
    }
    void enqueue_network_error() { script_.push_back(Step{{}, true}); }

    HttpResponse send(const HttpRequest& request) override {
        requests.push_back(request);
        Step step{HttpResponse{200, R"({"ok":true,"result":null})"}, false};
        if (!script_.empty()) {
            step = script_[std::min(script_.size() - 1, requests.size() - 1)];
        }
        if (step.throw_network_error) {
            throw NetworkError("mock network failure");
        }
        return step.response;
    }

    std::vector<HttpRequest> requests;

private:
    std::vector<Step> script_;
};

}  // namespace tgbot::test
