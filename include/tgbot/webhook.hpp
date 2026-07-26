#pragma once

/// @file
/// @brief Embedded webhook HTTP server.
///
/// The server speaks plain HTTP: in production TLS terminates in front of the
/// bot (a reverse proxy or a cloudflared tunnel), exactly like the test
/// environment this library ships with.

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "tgbot/dispatcher.hpp"

namespace tgbot {

/// Configuration of tgbot::WebhookServer.
struct WebhookOptions {
    /// Interface to bind, e.g. "127.0.0.1" behind a tunnel or "0.0.0.0".
    std::string host = "127.0.0.1";
    /// TCP port; 0 picks a free port (see WebhookServer::port()).
    int port = 8443;
    /// URL path Telegram POSTs updates to.
    std::string path = "/";
    /// When non-empty, every request must carry the matching
    /// @c X-Telegram-Bot-Api-Secret-Token header; mismatches get 403.
    /// Pass the same value as @c secret_token to setWebhook.
    std::string secret_token{};
    /// Threads dispatching updates to handlers after the 200 is sent.
    std::size_t worker_threads = 4;
    /// Bound on updates awaiting dispatch; when full the server answers 503
    /// and Telegram redelivers later (prevents memory exhaustion when
    /// handlers fall behind).
    std::size_t max_queue_size = 10'000;
    /// Maximum accepted request body size in bytes (Telegram updates are far
    /// below 1 MiB; oversized requests are rejected before buffering).
    std::size_t max_request_body = 8u * 1024 * 1024;
    /// Optional "answer the webhook with a method" hook, invoked inline
    /// before the HTTP response.  Returning a JSON object with a "method" key
    /// (see tgbot::answer_webhook_with) sends it as the response body, saving
    /// one round trip; returning nullopt sends a plain 200.  Regular
    /// dispatcher handlers still run afterwards on the worker pool.
    std::function<std::optional<nlohmann::json>(const Update&)> sync_responder{};
};

/// Builds an answer-webhook payload: @p params plus the @c method field.
/// @code
/// return tgbot::answer_webhook_with("sendMessage",
///                                   {{"chat_id", id}, {"text", "hi"}});
/// @endcode
/// @see https://core.telegram.org/bots/api#making-requests-when-getting-updates
nlohmann::json answer_webhook_with(const std::string& method, nlohmann::json params);

/// @brief Receives Telegram webhook updates and feeds a tgbot::Dispatcher.
///
/// Requests are answered 200 immediately; updates are parsed and dispatched
/// on a worker thread pool.  Register the endpoint with Api::setWebhook
/// (helpers generated like any other method) and verify delivery with
/// Api::getWebhookInfo.
class WebhookServer {
public:
    /// Binds the server logic to @p dispatcher (must outlive the server).
    WebhookServer(const Dispatcher& dispatcher, WebhookOptions options = {});
    ~WebhookServer();

    WebhookServer(const WebhookServer&) = delete;
    WebhookServer& operator=(const WebhookServer&) = delete;

    /// Binds and serves on a background thread; returns once the port is
    /// accepting connections (false when binding failed).
    bool start();

    /// Serves on the calling thread until stop() is called.  Binds first;
    /// throws tgbot::Error when the address cannot be bound.
    void run();

    /// Stops accepting, drains the worker pool, and joins all threads.
    /// Idempotent; a stopped server stays stopped (create a new one to
    /// serve again).
    /// @throws tgbot::Error when called from inside an update handler (a
    /// worker cannot join itself) — stop from any other thread instead.
    void stop();

    /// The port actually bound (useful with port 0).
    int port() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tgbot
