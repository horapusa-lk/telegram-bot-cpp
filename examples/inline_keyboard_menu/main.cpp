// Inline keyboard menu: /menu shows buttons, taps edit the message in place.

#include <cstdio>

#include <tgbot/tgbot.hpp>

#include "../common.hpp"

int main() {
    tgbot::Bot bot(examples::require_env("TG_BOT_TOKEN"));

    bot.onCommand("menu", [&](const tgbot::Message& m) {
        bot.api().sendMessage({
            .chat_id = m.chat.id,
            .text = "Pick a flavor:",
            .reply_markup = tgbot::InlineKeyboardBuilder()
                                .text("Vanilla", "flavor:vanilla")
                                .text("Chocolate", "flavor:chocolate")
                                .row()
                                .url("More flavors", "https://en.wikipedia.org/wiki/Ice_cream")
                                .build(),
        });
    });

    bot.onCallbackQuery([&](const tgbot::CallbackQuery& q) {
        // Always answer the query so the client stops the spinner.
        bot.api().answerCallbackQuery({.callback_query_id = q.id});

        const std::string choice = q.data.value_or("");
        if (choice.rfind("flavor:", 0) != 0 || !q.message.has_value()) {
            return;
        }
        const auto* accessible = std::get_if<tgbot::Message>(&*q.message);
        if (accessible == nullptr) {
            return;  // message too old to edit
        }
        bot.api().editMessageText({
            .chat_id = accessible->chat.id,
            .message_id = accessible->message_id,
            .text = "You picked " + choice.substr(7) + "!",
        });
    });

    std::printf("send /menu to the bot\n");
    bot.startPolling();
    return 0;
}
