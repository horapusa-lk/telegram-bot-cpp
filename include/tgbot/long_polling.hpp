#pragma once

/// @file
/// @brief getUpdates long polling with offset management and self-healing.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "tgbot/api.hpp"
#include "tgbot/dispatcher.hpp"

namespace tgbot {

/// Tunables for tgbot::LongPoller.
struct LongPollOptions {
    /// Long-poll duration passed to getUpdates (seconds).  Telegram holds the
    /// request open up to this long; higher values mean fewer requests.
    std::int64_t timeout_seconds = 50;
    /// Maximum updates per batch (Telegram default 100).
    std::optional<std::int64_t> limit{};
    /// Update types to receive; unset keeps the server-side default.
    /// @see https://core.telegram.org/bots/api#getupdates
    std::optional<std::vector<std::string>> allowed_updates{};
    /// First delay after a transport/API failure; doubles per consecutive
    /// failure up to @ref max_error_backoff, resets on success.
    std::chrono::milliseconds error_backoff{2'000};
    /// Upper bound for the failure backoff.
    std::chrono::milliseconds max_error_backoff{30'000};
};

/// @brief Pulls updates via getUpdates and feeds a tgbot::Dispatcher.
///
/// The loop maintains the offset (highest dispatched update_id + 1, committed
/// only after the batch is handed to the dispatcher), recovers from network
/// errors with capped exponential backoff, and shuts down gracefully: stop()
/// returns immediately and run() exits as soon as the in-flight poll
/// completes.
///
/// @code
/// tgbot::Api api(token);
/// tgbot::Dispatcher dispatcher;
/// dispatcher.onUpdate([&](const tgbot::Update& u) { ... });
/// tgbot::LongPoller poller(api, dispatcher);
/// poller.run();  // blocks; call poller.stop() from a handler or elsewhere
/// @endcode
class LongPoller {
public:
    /// Binds the poller to an API client and a dispatcher; both must outlive
    /// the poller.
    LongPoller(const Api& api, const Dispatcher& dispatcher, LongPollOptions options = {});

    /// Runs the polling loop on the calling thread until stop() is called.
    /// A poller whose stop() was already requested returns immediately: a
    /// stopped poller stays stopped (construct a new one to poll again), so
    /// a stop() racing run() startup can never be lost.
    void run();

    /// Requests shutdown; safe to call from any thread, including handlers.
    void stop();

    /// True while run() is executing.
    bool running() const noexcept { return running_.load(); }

    /// The next getUpdates offset (exposed for tests and checkpointing).
    std::int64_t offset() const noexcept { return offset_.load(); }

private:
    bool wait_backoff(std::chrono::milliseconds delay);

    const Api& api_;
    const Dispatcher& dispatcher_;
    LongPollOptions options_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::atomic<std::int64_t> offset_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace tgbot
