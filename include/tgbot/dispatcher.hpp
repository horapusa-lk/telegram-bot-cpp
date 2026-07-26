#pragma once

/// @file
/// @brief The mode-agnostic update pipeline: handlers registered here receive
/// updates identically under long polling and webhooks.

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

#include "tgbot/types.hpp"

namespace tgbot {

/// Callback invoked for every incoming tgbot::Update.
using UpdateHandler = std::function<void(const Update&)>;

/// Callback receiving exceptions thrown by update handlers (and, for the
/// webhook server, payloads that failed to parse).
using ErrorHandler = std::function<void(std::exception_ptr)>;

/// @brief Fans incoming updates out to registered handlers.
///
/// Both tgbot::LongPoller and tgbot::WebhookServer feed a Dispatcher, so user
/// code is identical in either mode.  Handlers run on the delivering thread
/// (the polling thread, or a webhook worker).  A handler exception never
/// tears down the delivery loop: it is routed to the error handler (or
/// swallowed when none is set).
///
/// Thread-safe: handlers may be added while updates are flowing.
class Dispatcher {
public:
    /// Registers a catch-all update handler; handlers run in registration
    /// order.
    void onUpdate(UpdateHandler handler);

    /// Registers the sink for handler exceptions.
    void onError(ErrorHandler handler);

    /// Invokes every registered handler for @p update.
    void dispatch(const Update& update) const;

    /// Parses a raw update payload (webhook body, getUpdates array element)
    /// and dispatches it.
    /// @throws nlohmann::json::exception when the payload is not an Update.
    void dispatch_json(const nlohmann::json& payload) const;

    /// Delivers @p error to the registered error handler (no-op without one).
    /// Used by delivery backends for failures outside any handler call.
    void dispatch_error(std::exception_ptr error) const;

private:
    struct Handlers {
        std::vector<UpdateHandler> update;
        ErrorHandler error;
    };
    std::shared_ptr<const Handlers> snapshot() const;

    mutable std::mutex mutex_;
    std::shared_ptr<const Handlers> handlers_ = std::make_shared<Handlers>();
};

}  // namespace tgbot
