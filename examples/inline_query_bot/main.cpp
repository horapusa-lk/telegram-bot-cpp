// Inline mode: type @YourBot <text> in any chat to get shout/whisper articles.
// Enable inline mode for the bot via @BotFather (/setinline) first.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include <tgbot/tgbot.hpp>

#include "../common.hpp"

namespace {

tgbot::InlineQueryResult make_article(const std::string& id, const std::string& title,
                                      const std::string& text) {
    tgbot::InputTextMessageContent content;
    content.message_text = text;

    tgbot::InlineQueryResultArticle article;
    article.id = id;
    article.title = title;
    article.input_message_content = tgbot::InputMessageContent(std::move(content));
    return article;
}

}  // namespace

int main() {
    tgbot::Bot bot(examples::require_env("TG_BOT_TOKEN"));

    bot.onInlineQuery([&](const tgbot::InlineQuery& q) {
        if (q.query.empty()) {
            return;
        }
        std::string shout = q.query;
        std::transform(shout.begin(), shout.end(), shout.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        bot.api().answerInlineQuery({
            .inline_query_id = q.id,
            .results = {make_article("shout", "Shout it", shout + "!!!"),
                        make_article("whisper", "Whisper it", "(" + q.query + ")")},
            .cache_time = 0,
        });
    });

    std::printf("inline bot ready — type @YourBot something in any chat\n");
    bot.startPolling();
    return 0;
}
