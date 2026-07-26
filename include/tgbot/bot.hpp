#pragma once

/// @file
/// @brief The Bot facade: message/command routing on top of Api + Dispatcher.

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "tgbot/api.hpp"
#include "tgbot/dispatcher.hpp"
#include "tgbot/long_polling.hpp"
#include "tgbot/webhook.hpp"

namespace tgbot {

/// @brief High-level entry point tying everything together.
///
/// Owns the tgbot::Api, a tgbot::Dispatcher with a routing layer, and the
/// delivery mode you start.  Handler code is identical under polling and
/// webhooks:
///
/// @code
/// tgbot::Bot bot(token);
/// bot.onCommand("start", [&](const tgbot::Message& m) {
///     bot.api().sendMessage({.chat_id = m.chat.id, .text = "hi!"});
/// });
/// bot.startPolling();               // or bot.startWebhook({...})
/// @endcode
class Bot {
public:
    /// Handler for routed messages.
    using MessageHandler = std::function<void(const Message&)>;
    /// Handler for callback queries.
    using CallbackQueryHandler = std::function<void(const CallbackQuery&)>;
    /// Handler for inline queries.
    using InlineQueryHandler = std::function<void(const InlineQuery&)>;

    /// Creates a bot for @p token.
    explicit Bot(std::string token, ApiClient::Options client_options = {});

    /// Wraps an existing Api (custom transport in tests, shared client, ...).
    explicit Bot(Api api);

    ~Bot();
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;

    /// @name Routing
    /// Handlers of one kind run in registration order.  A message that
    /// matches a command handler is consumed: plain onMessage handlers do
    /// not see it.
    /// @{

    /// Routes "/name" commands ("/name@YourBot args" included).  @p command
    /// may be given with or without the leading slash; matching is
    /// case-insensitive per Telegram convention.
    void onCommand(std::string command, MessageHandler handler);

    /// Routes messages that are not consumed by a command handler.
    void onMessage(MessageHandler handler);

    /// Routes callback queries from inline keyboards.
    void onCallbackQuery(CallbackQueryHandler handler);

    /// Routes inline queries.
    void onInlineQuery(InlineQueryHandler handler);

    /// Catch-all: sees every update, before and independent of routing.
    void onAnyUpdate(UpdateHandler handler);

    /// Sink for exceptions thrown by handlers.
    void onError(ErrorHandler handler);
    /// @}

    /// @name Delivery
    /// @{

    /// Runs long polling on the calling thread until stopPolling().
    void startPolling(LongPollOptions options = {});

    /// Stops a running startPolling() loop (callable from handlers).
    void stopPolling();

    /// Starts the webhook server in the background and returns it (throws
    /// tgbot::Error when the bind fails).  Call Api::setWebhook yourself with
    /// the public URL — see the polling-vs-webhook guide.
    WebhookServer& startWebhook(WebhookOptions options);

    /// Stops the webhook server if one is running.
    void stopWebhook();
    /// @}

    /// The typed API surface.
    Api& api() noexcept { return api_; }
    /// @copydoc api()
    const Api& api() const noexcept { return api_; }

    /// The underlying dispatcher (advanced use; routing is layered on it).
    Dispatcher& dispatcher() noexcept { return dispatcher_; }

private:
    struct Router;
    void route(const Update& update) const;

    Api api_;
    Dispatcher dispatcher_;
    std::shared_ptr<Router> router_;
    mutable std::mutex lifecycle_mutex_;  // guards poller_/webhook_ swaps
    std::shared_ptr<LongPoller> poller_;
    std::unique_ptr<WebhookServer> webhook_;
};

}  // namespace tgbot
