#pragma once

#include <chrono>
#include <string>

#include "tgbot/http_client.hpp"

namespace tgbot {

/// @brief Production tgbot::HttpClient implementation backed by libcurl.
///
/// Thread-safe: each thread reuses its own easy handle, so long-polling and
/// concurrent webhook worker threads all keep persistent HTTPS connections.
class CurlHttpClient final : public HttpClient {
public:
    /// Tunables for the transport.
    struct Options {
        /// Timeout applied when a request does not carry its own.
        std::chrono::milliseconds default_timeout{80'000};
        /// Value of the @c User-Agent header.
        std::string user_agent = "tgbot-cpp-full";
        /// Cap on time spent establishing a connection.
        std::chrono::milliseconds connect_timeout{15'000};
    };

    CurlHttpClient();
    /// Constructs the client with explicit @p options.
    explicit CurlHttpClient(Options options);
    ~CurlHttpClient() override;

    /// Executes a blocking POST via libcurl.
    /// @throws tgbot::NetworkError on any curl-level failure.
    HttpResponse send(const HttpRequest& request) override;

private:
    Options options_;
};

}  // namespace tgbot
