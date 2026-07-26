// Minimal long-polling echo bot.
//
//   TG_BOT_TOKEN=... ./echo_polling
//
// /start greets, /stop shuts the bot down, anything else is echoed back.

#include <cstdio>

#include <tgbot/tgbot.hpp>

#include "../common.hpp"

int main() {
    tgbot::Bot bot(examples::require_env("TG_BOT_TOKEN"));

    bot.onCommand("start", [&](const tgbot::Message& m) {
        bot.api().sendMessage({
            .chat_id = m.chat.id,
            .text = "Hello! Send me anything and I will echo it.",
        });
    });

    bot.onCommand("stop", [&](const tgbot::Message& m) {
        bot.api().sendMessage({.chat_id = m.chat.id, .text = "Bye!"});
        bot.stopPolling();
    });

    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text.has_value()) {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
        }
    });

    bot.onError([](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "handler error: %s\n", e.what());
        }
    });

    const tgbot::User me = bot.api().getMe();
    std::printf("polling as @%s (Bot API %s)\n", me.username.value_or("?").c_str(),
                std::string(tgbot::api_version()).c_str());
    bot.startPolling();
    return 0;
}
