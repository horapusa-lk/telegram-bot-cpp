#include "tgbot/long_polling.hpp"

#include <algorithm>
#include <utility>

namespace tgbot {

LongPoller::LongPoller(const Api& api, const Dispatcher& dispatcher, LongPollOptions options)
    : api_(api), dispatcher_(dispatcher), options_(std::move(options)) {}

void LongPoller::stop() {
    {
        // The store must happen under the mutex, otherwise wait_backoff()
        // can evaluate its predicate, miss this notification, and sleep the
        // whole backoff despite the promptness promise.
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_.store(true);
    }
    cv_.notify_all();
}

bool LongPoller::wait_backoff(std::chrono::milliseconds delay) {
    std::unique_lock<std::mutex> lock(mutex_);
    return !cv_.wait_for(lock, delay, [this] { return stop_requested_.load(); });
}

void LongPoller::run() {
    // A stop() that arrived before run() must win: never reset the flag here
    // (a stopped poller stays stopped; construct a new one to poll again).
    running_.store(true);
    auto backoff = options_.error_backoff;

    while (!stop_requested_.load()) {
        std::vector<Update> updates;
        try {
            GetUpdatesParams params;
            if (offset_.load() != 0) {
                params.offset = offset_.load();
            }
            params.limit = options_.limit;
            params.timeout = options_.timeout_seconds;
            params.allowed_updates = options_.allowed_updates;
            updates = api_.getUpdates(params);
            backoff = options_.error_backoff;  // healthy again
        } catch (const Error&) {
            // Transient trouble (the ApiClient already retried): back off and
            // poll again; the interruptible wait keeps stop() prompt.
            if (!wait_backoff(backoff)) {
                break;
            }
            backoff = std::min(backoff * 2, options_.max_error_backoff);
            continue;
        }

        for (const Update& update : updates) {
            dispatcher_.dispatch(update);
            // Commit only after dispatch so a crash cannot skip updates.
            offset_.store(std::max(offset_.load(), update.update_id + 1));
            if (stop_requested_.load()) {
                break;
            }
        }
    }
    running_.store(false);
}

}  // namespace tgbot
