#include "tgbot/bot.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <vector>

namespace tgbot {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Extracts "name" out of a leading "/name" or "/name@SomeBot" command; empty
/// when the message is not a command.
std::string command_of(const Message& message) {
    const std::string* text = nullptr;
    if (message.text.has_value()) {
        text = &*message.text;
    } else if (message.caption.has_value()) {
        text = &*message.caption;
    }
    if (text == nullptr || text->empty() || (*text)[0] != '/') {
        return {};
    }
    const auto end = text->find_first_of(" \t\n");
    std::string cmd = text->substr(1, end == std::string::npos ? end : end - 1);
    if (const auto at = cmd.find('@'); at != std::string::npos) {
        cmd.erase(at);
    }
    return to_lower(cmd);
}

}  // namespace

struct Bot::Router {
    std::mutex mutex;
    std::map<std::string, std::vector<MessageHandler>> commands;
    std::vector<MessageHandler> messages;
    std::vector<CallbackQueryHandler> callback_queries;
    std::vector<InlineQueryHandler> inline_queries;

    template <typename T>
    std::vector<T> snapshot(const std::vector<T>& v) {
        const std::lock_guard<std::mutex> lock(mutex);
        return v;
    }
};

Bot::Bot(std::string token, ApiClient::Options client_options)
    : Bot(Api(std::move(token), std::move(client_options))) {}

Bot::Bot(Api api) : api_(std::move(api)), router_(std::make_shared<Router>()) {
    dispatcher_.onUpdate([this](const Update& update) { route(update); });
}

Bot::~Bot() {
    stopPolling();
    stopWebhook();
}

void Bot::route(const Update& update) const {
    if (update.message.has_value()) {
        const Message& message = *update.message;
        const std::string cmd = command_of(message);
        if (!cmd.empty()) {
            std::vector<MessageHandler> handlers;
            {
                const std::lock_guard<std::mutex> lock(router_->mutex);
                if (const auto it = router_->commands.find(cmd); it != router_->commands.end()) {
                    handlers = it->second;
                }
            }
            if (!handlers.empty()) {
                for (const auto& h : handlers) {
                    h(message);
                }
                return;  // consumed by the command layer
            }
        }
        for (const auto& h : router_->snapshot(router_->messages)) {
            h(message);
        }
    }
    if (update.callback_query.has_value()) {
        for (const auto& h : router_->snapshot(router_->callback_queries)) {
            h(*update.callback_query);
        }
    }
    if (update.inline_query.has_value()) {
        for (const auto& h : router_->snapshot(router_->inline_queries)) {
            h(*update.inline_query);
        }
    }
}

void Bot::onCommand(std::string command, MessageHandler handler) {
    if (!command.empty() && command[0] == '/') {
        command.erase(0, 1);
    }
    const std::lock_guard<std::mutex> lock(router_->mutex);
    router_->commands[to_lower(command)].push_back(std::move(handler));
}

void Bot::onMessage(MessageHandler handler) {
    const std::lock_guard<std::mutex> lock(router_->mutex);
    router_->messages.push_back(std::move(handler));
}

void Bot::onCallbackQuery(CallbackQueryHandler handler) {
    const std::lock_guard<std::mutex> lock(router_->mutex);
    router_->callback_queries.push_back(std::move(handler));
}

void Bot::onInlineQuery(InlineQueryHandler handler) {
    const std::lock_guard<std::mutex> lock(router_->mutex);
    router_->inline_queries.push_back(std::move(handler));
}

void Bot::onAnyUpdate(UpdateHandler handler) {
    dispatcher_.onUpdate(std::move(handler));
}

void Bot::onError(ErrorHandler handler) {
    dispatcher_.onError(std::move(handler));
}

void Bot::startPolling(LongPollOptions options) {
    // Publish the poller under the lifecycle mutex so a concurrent
    // stopPolling() either sees it (and stops it) or precedes it (and the
    // poller-side sticky stop flag still wins); run() happens outside the
    // lock because it blocks.
    std::shared_ptr<LongPoller> poller;
    {
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        poller_ = std::make_shared<LongPoller>(api_, dispatcher_, std::move(options));
        poller = poller_;
    }
    poller->run();
}

void Bot::stopPolling() {
    std::shared_ptr<LongPoller> poller;
    {
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        poller = poller_;
    }
    if (poller) {
        poller->stop();
    }
}

WebhookServer& Bot::startWebhook(WebhookOptions options) {
    const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    webhook_ = std::make_unique<WebhookServer>(dispatcher_, std::move(options));
    if (!webhook_->start()) {
        webhook_.reset();
        throw Error("Bot: webhook server failed to bind");
    }
    return *webhook_;
}

void Bot::stopWebhook() {
    std::unique_ptr<WebhookServer> webhook;
    {
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        webhook = std::move(webhook_);
    }
    if (webhook) {
        webhook->stop();
    }
}

}  // namespace tgbot
