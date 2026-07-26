// The same echo bot, delivered over a webhook.
//
//   TG_BOT_TOKEN=...  TG_WEBHOOK_URL=https://your-tunnel.example.com \
//   TG_WEBHOOK_BIND=127.0.0.1:8591  ./echo_webhook
//
// The server speaks plain HTTP on the bind address; TLS terminates at the
// public endpoint in front of it (e.g. a cloudflared tunnel).  See
// docs/polling-vs-webhook.md for the full tunnel walkthrough.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <tgbot/tgbot.hpp>

#include "../common.hpp"

int main() {
    tgbot::Bot bot(examples::require_env("TG_BOT_TOKEN"));
    const std::string public_url = examples::require_env("TG_WEBHOOK_URL");
    const std::string bind = examples::require_env("TG_WEBHOOK_BIND");

    const auto colon = bind.rfind(':');
    const std::string host = colon == std::string::npos ? bind : bind.substr(0, colon);
    const int port = colon == std::string::npos ? 8443 : std::stoi(bind.substr(colon + 1));

    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text.has_value()) {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
        }
    });

    // Any hard-to-guess value works; Telegram echoes it back in the
    // X-Telegram-Bot-Api-Secret-Token header and the server enforces it.
    const std::string secret = "example-secret-" + std::to_string(port);

    tgbot::WebhookOptions options;
    options.host = host;
    options.port = port;
    options.secret_token = secret;
    bot.startWebhook(options);

    bot.api().setWebhook({.url = public_url, .secret_token = secret});
    std::printf("webhook registered: %s -> %s:%d\n", public_url.c_str(), host.c_str(), port);

    for (;;) {  // serve until interrupted (Ctrl+C); Telegram keeps delivering
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}
