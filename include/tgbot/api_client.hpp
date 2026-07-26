#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tgbot/api_error.hpp"
#include "tgbot/http_client.hpp"

namespace tgbot {

/// @brief Automatic retry behavior for API calls.
///
/// Two classes of failure are retried:
///  - rate limiting: HTTP 429 with @c retry_after — the client sleeps the
///    server-requested duration (bounded by @ref max_retry_after) and repeats;
///  - transient faults: network errors and HTTP 5xx — retried with
///    exponential backoff starting at @ref initial_backoff.
///
/// Every other Bot API error is thrown immediately as tgbot::ApiError.
///
/// @warning A network error can strike after a request was fully delivered
/// (e.g. the response timed out), so retrying may execute a non-idempotent
/// method twice — a duplicate sendMessage is the classic case.  The Bot API
/// offers no idempotency keys; set @ref retry_on_network_error to false for
/// calls where a duplicate is worse than a failure.
///
/// @note Retry sleeps block the calling thread and are not cancellable; the
/// worst case per call is roughly @ref max_retry_after times the retry
/// budget.  Lower @ref max_retry_after (or disable
/// @ref retry_on_rate_limit) for latency-sensitive callers.
struct RetryPolicy {
    /// Total attempts per call (1 = no retries).
    int max_attempts = 3;
    /// First backoff delay for transient faults; doubles each retry via
    /// @ref backoff_multiplier.
    std::chrono::milliseconds initial_backoff{500};
    /// Factor applied to the backoff after each failed attempt.
    double backoff_multiplier = 2.0;
    /// Rate-limit waits longer than this give up immediately (the ApiError
    /// with @c retry_after is thrown instead so the caller can schedule).
    std::chrono::seconds max_retry_after{60};
    /// Honor 429 @c retry_after by sleeping and retrying.
    bool retry_on_rate_limit = true;
    /// Retry HTTP 5xx responses (Bot API hiccups, proxy errors).
    bool retry_on_server_error = true;
    /// Retry tgbot::NetworkError failures (see the duplicate-send warning
    /// above).
    bool retry_on_network_error = true;
};

/// @brief Low-level Bot API caller: owns the token, endpoint, transport and
/// retry policy, and speaks the JSON / multipart request envelope.
///
/// The generated tgbot::Api sits on top of this class; use ApiClient directly
/// only to issue raw calls.  Thread-safe.
class ApiClient {
public:
    /// Construction options.
    struct Options {
        /// API endpoint; change to use a Local Bot API Server.
        std::string base_url = "https://api.telegram.org";
        /// Retry/backoff configuration.
        RetryPolicy retry;
        /// Default per-request transport timeout.
        std::chrono::milliseconds request_timeout{80'000};
        /// Transport override; defaults to a shared tgbot::CurlHttpClient.
        std::shared_ptr<HttpClient> http{};
    };

    /// Creates a client for @p token with default options.
    explicit ApiClient(std::string token);

    /// Creates a client for @p token (the value BotFather issued, kept out of
    /// all error messages).  Two overloads instead of a default argument: a
    /// brace-defaulted nested aggregate in its enclosing class is rejected by
    /// GCC and Clang.
    ApiClient(std::string token, Options options);

    /// Calls @p method with JSON @p params.
    ///
    /// With no @p files the request is a JSON POST; otherwise it is promoted
    /// to @c multipart/form-data (each param becomes a form field, each file a
    /// file part referenced via @c attach://).  Returns the @c result payload
    /// of the envelope.
    ///
    /// @param method API method name, e.g. @c "sendMessage".
    /// @param params top-level JSON object of parameters (may be empty).
    /// @param files upload parts collected from tgbot::InputFile params.
    /// @param timeout_override transport timeout for this call; zero uses the
    ///        configured default (long polling passes poll timeout + slack).
    /// @throws tgbot::ApiError when the API rejects the call.
    /// @throws tgbot::NetworkError when transport fails beyond the retry budget.
    nlohmann::json call(const std::string& method, nlohmann::json params,
                        std::vector<MultipartPart> files = {},
                        std::chrono::milliseconds timeout_override = {}) const;

    /// The bot token this client authenticates with.
    const std::string& token() const noexcept { return token_; }

    /// Configured options (read-only).
    const Options& options() const noexcept { return options_; }

private:
    HttpResponse send_once(const std::string& method, const nlohmann::json& params,
                           const std::vector<MultipartPart>& files,
                           std::chrono::milliseconds timeout) const;

    std::string token_;
    Options options_;
};

}  // namespace tgbot
