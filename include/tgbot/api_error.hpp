#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace tgbot {

/// @brief Base class of all exceptions thrown by this library.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Transport-level failure: DNS, TLS, connection reset, timeout, or a
/// response that is not a valid Bot API envelope (e.g. a proxy error page).
///
/// Transient network errors are retried automatically according to the
/// tgbot::RetryPolicy before this exception reaches the caller.
class NetworkError : public Error {
public:
    using Error::Error;
};

/// @brief An error returned by the Bot API itself (@c ok=false envelope).
///
/// Carries the HTTP-like @c error_code and human-readable @c description from
/// the response, plus the fields of the @c ResponseParameters object when
/// present: @c retry_after (rate limiting, honored automatically per
/// tgbot::RetryPolicy before this is thrown) and @c migrate_to_chat_id (the
/// group was upgraded to a supergroup).
///
/// @see https://core.telegram.org/bots/api#making-requests
/// @see https://core.telegram.org/bots/api#responseparameters
class ApiError : public Error {
public:
    /// Builds the exception; @p retry_after / @p migrate_to_chat_id come from
    /// the optional @c parameters object of the error envelope.
    ApiError(int error_code, std::string description, std::optional<int> retry_after = {},
             std::optional<std::int64_t> migrate_to_chat_id = {});

    /// Numeric error code from the envelope (e.g. 400, 403, 429).
    int error_code() const noexcept { return error_code_; }

    /// Human-readable explanation of the error from the envelope.
    const std::string& description() const noexcept { return description_; }

    /// Seconds to wait before repeating the request, when the error is a rate
    /// limit (HTTP 429); mirrors @c ResponseParameters.retry_after.
    std::optional<int> retry_after() const noexcept { return retry_after_; }

    /// The supergroup id a group chat migrated to; mirrors
    /// @c ResponseParameters.migrate_to_chat_id.
    std::optional<std::int64_t> migrate_to_chat_id() const noexcept { return migrate_to_chat_id_; }

private:
    int error_code_;
    std::string description_;
    std::optional<int> retry_after_;
    std::optional<std::int64_t> migrate_to_chat_id_;
};

}  // namespace tgbot
