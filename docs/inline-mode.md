# Inline Mode

Inline mode lets users summon your bot in *any* chat by typing `@YourBot query` — no need to add the bot to the chat. The bot answers with a horizontal list of results; whatever the user picks is sent into the conversation on their behalf. See the official overview at [core.telegram.org/bots/api#inline-mode](https://core.telegram.org/bots/api#inline-mode).

This guide covers receiving queries with `Bot::onInlineQuery`, building `tgbot::InlineQueryResult` values, answering with `Api::answerInlineQuery`, collecting `ChosenInlineResult` feedback, and `Api::answerWebAppQuery`.

## Enabling inline mode

Inline mode is off by default. Turn it on in a chat with [@BotFather](https://t.me/BotFather):

1. Send `/setinline`, pick your bot, and enter the placeholder text shown in the input field (e.g. `Search quotes...`).
2. Optionally send `/setinlinefeedback` to receive [ChosenInlineResult](https://core.telegram.org/bots/api#choseninlineresult) updates — see [Chosen-result feedback](#chosen-result-feedback) below.

Until `/setinline` is done, Telegram never sends your bot an `inline_query` update, so `onInlineQuery` will simply never fire.

## Receiving inline queries

Register a handler with `Bot::onInlineQuery`. It receives a `tgbot::InlineQuery` with the fields you would expect from the [official type](https://core.telegram.org/bots/api#inlinequery):

- `id` — the query id you must echo back in `answerInlineQuery`
- `from` — the `User` typing the query
- `query` — the text after `@YourBot` (up to 256 characters, may be empty)
- `offset` — pagination cursor previously returned by you via `next_offset`
- `chat_type`, `location` — optional context (`std::optional`)

A complete minimal bot (this is essentially `examples/inline_query_bot/main.cpp`):

```cpp
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include <tgbot/tgbot.hpp>

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
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onInlineQuery([&](const tgbot::InlineQuery& q) {
        if (q.query.empty()) {
            return;  // or answer with trending/default results
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

    bot.startPolling();
    return 0;
}
```

Telegram sends a new `inline_query` update on nearly every keystroke, so keep the handler fast and answer exactly once per query id. Answering the same id twice, or too late, raises a `tgbot::ApiError` — see [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md).

## Building results

`tgbot::InlineQueryResult` is a `std::variant` over the twenty [InlineQueryResult](https://core.telegram.org/bots/api#inlinequeryresult) structs (`InlineQueryResultArticle`, `InlineQueryResultPhoto`, `InlineQueryResultCachedPhoto`, `InlineQueryResultGif`, `InlineQueryResultVideo`, ...). Fill the concrete struct and let it convert into the variant — no manual tagging needed.

Two fields follow the same discipline in every variant:

- **`id`** — *you* must set it: a unique string, 1–64 bytes, distinct across the results of one answer. It comes back to you verbatim in `ChosenInlineResult::result_id`, so encode something meaningful (a database key, an index).
- **`type`** — pre-filled by the library (`InlineQueryResultArticle` defaults to `type = "article"`, both photo variants to `"photo"`, and so on). Leave it alone; it exists so serialization matches the wire format.

### Article — text results

`InlineQueryResultArticle` is the workhorse for text: a title (plus optional `description`, `url`, `thumbnail_url`) in the result list, and an `input_message_content` that becomes the actual message when picked. `input_message_content` is a `tgbot::InputMessageContent` — another `std::variant`, most commonly holding an [`InputTextMessageContent`](https://core.telegram.org/bots/api#inputtextmessagecontent):

```cpp
tgbot::InputTextMessageContent content;
content.message_text = "*bold* result";
content.parse_mode = "MarkdownV2";

tgbot::InlineQueryResultArticle article;
article.id = "art-42";
article.title = "A formatted article";
article.description = "Shown under the title in the result list";
article.input_message_content = tgbot::InputMessageContent(std::move(content));
```

For an article, `input_message_content` is required. For media results it is optional — by default the media itself is sent.

### Photo — by URL

`InlineQueryResultPhoto` points at a public JPEG URL (max 5 MB) plus a thumbnail URL; Telegram fetches and sends the photo when chosen:

```cpp
tgbot::InlineQueryResultPhoto photo;
photo.id = "photo-1";
photo.photo_url = "https://example.com/cat.jpg";
photo.thumbnail_url = "https://example.com/cat_thumb.jpg";
photo.caption = "A cat";  // optional
```

All URLs you put in inline results are visible to end users — treat them as public.

### CachedPhoto — by file_id

`InlineQueryResultCachedPhoto` reuses a photo already on Telegram's servers via its `file_id` — instant to send, no re-upload, no public URL needed:

```cpp
tgbot::InlineQueryResultCachedPhoto cached;
cached.id = "photo-2";
cached.photo_file_id = known_file_id;  // e.g. saved from Message::photo after a sendPhoto
cached.caption = "Served from cache";
```

You obtain `file_id`s by sending the media once (to yourself, or a private channel) and storing the id from the returned `Message` — see [sending-files.md](sending-files.md). The same cached pattern exists for the other media kinds (`InlineQueryResultCachedGif`, `InlineQueryResultCachedVideo`, `InlineQueryResultCachedSticker`, ...).

### Attaching an inline keyboard

Every result variant has an optional `reply_markup` (`std::optional<InlineKeyboardMarkup>`). The builder from [keyboards-and-callbacks.md](keyboards-and-callbacks.md) works here too:

```cpp
article.reply_markup = tgbot::InlineKeyboardBuilder()
                           .text("More like this", "more:art-42")
                           .build();
```

Attaching a keyboard matters beyond the buttons themselves: only messages sent *with an inline keyboard* carry an `inline_message_id`, which you need to edit the message later (see feedback below).

## Answering: `answerInlineQuery`

`Api::answerInlineQuery` takes a `tgbot::AnswerInlineQueryParams` aggregate; use designated initializers in field order:

```cpp
struct AnswerInlineQueryParams {
    std::string inline_query_id;                        // echo InlineQuery::id
    std::vector<InlineQueryResult> results;             // up to 50 per answer
    std::optional<std::int64_t> cache_time;             // server-side cache, default 300 s
    std::optional<bool> is_personal;                    // cache per-user instead of globally
    std::optional<std::string> next_offset;             // pagination cursor, <= 64 bytes
    std::optional<InlineQueryResultsButton> button;     // button above the result list
};
```

- **`cache_time`** — Telegram caches the answer for this query text (default 300 s). During development set `cache_time = 0` so you always see fresh results.
- **`is_personal`** — set `true` when results depend on *who* asked (their bookmarks, their language); otherwise the cache is shared across all users typing the same query.
- **`next_offset`** — pagination: return up to 50 results plus the cursor for the next page. The client sends it back as `InlineQuery::offset` when the user scrolls. Empty string (or unset) means "no more pages".
- **`button`** — an [`InlineQueryResultsButton`](https://core.telegram.org/bots/api#inlinequeryresultsbutton) shown above the results: `text` plus either a `start_parameter` (deep-links the user into a private `/start` chat with your bot) or a `web_app`.

Pagination and a setup button together:

```cpp
bot.onInlineQuery([&](const tgbot::InlineQuery& q) {
    const std::size_t page = q.offset.empty() ? 0 : std::stoul(q.offset);
    auto results = search(q.query, page);  // your code: at most 50 per page

    tgbot::InlineQueryResultsButton connect;
    connect.text = "Connect your account";
    connect.start_parameter = "connect";  // arrives as "/start connect" in private chat

    bot.api().answerInlineQuery({
        .inline_query_id = q.id,
        .results = std::move(results),
        .is_personal = true,
        .next_offset = has_more ? std::to_string(page + 1) : std::string{},
        .button = connect,
    });
});
```

## Chosen-result feedback

With `/setinlinefeedback` enabled in @BotFather, Telegram reports which result a user actually picked as a `chosen_inline_result` update. (BotFather lets you choose a sampling percentage — 100 % is fine for stats-sized bots, lower for high-traffic ones.)

There is no dedicated router hook for it; use the catch-all `Bot::onAnyUpdate` and check `Update::chosen_inline_result`:

```cpp
bot.onAnyUpdate([&](const tgbot::Update& u) {
    if (!u.chosen_inline_result) {
        return;
    }
    const tgbot::ChosenInlineResult& chosen = *u.chosen_inline_result;
    // chosen.result_id  — the id you assigned to the result
    // chosen.query      — the query that produced it
    // chosen.from       — who sent it
    record_pick(chosen.from.id, chosen.result_id, chosen.query);

    if (chosen.inline_message_id) {
        // Present only when the result carried an inline keyboard.
        bot.api().editMessageText({
            .inline_message_id = *chosen.inline_message_id,
            .text = "Delivered via @" + std::string("YourBot"),
        });
    }
});
```

`ChosenInlineResult::inline_message_id` is your only handle on the sent message — there is no chat id, because the message lives in someone else's chat. It exists only when the result had a `reply_markup`, and the same id later appears on `CallbackQuery::inline_message_id` when the buttons are pressed. Edit methods such as `Api::editMessageText` accept it through their optional `inline_message_id` field in place of `chat_id` + `message_id`.

One caveat: if you set `LongPollOptions::allowed_updates` (or the equivalent on `setWebhook`) explicitly, include `"chosen_inline_result"` in the list or the updates are filtered out server-side.

## `answerWebAppQuery`

When a [Web App](https://core.telegram.org/bots/webapps) launched from inline mode (e.g. via `InlineQueryResultsButton::web_app`) wants to post a message back into the originating chat, the app hands your backend a `web_app_query_id`, and you answer it with a single result:

```cpp
tgbot::InputTextMessageContent content;
content.message_text = "Result chosen in the Web App";

tgbot::InlineQueryResultArticle article;
article.id = "webapp-1";
article.title = "unused";  // required by the type, not displayed
article.input_message_content = tgbot::InputMessageContent(std::move(content));

tgbot::SentWebAppMessage sent = bot.api().answerWebAppQuery({
    .web_app_query_id = query_id,
    .result = article,
});
// sent.inline_message_id is set when the result carried an inline keyboard
```

`AnswerWebAppQueryParams` has exactly two fields — `web_app_query_id` and one `InlineQueryResult` — and returns a [`SentWebAppMessage`](https://core.telegram.org/bots/api#sentwebappmessage) whose optional `inline_message_id` follows the same keyboard rule as above.

## See also

- [keyboards-and-callbacks.md](keyboards-and-callbacks.md) — `InlineKeyboardBuilder`, handling the callback queries your inline results generate
- [sending-files.md](sending-files.md) — obtaining the `file_id`s that cached results reuse
- [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md) — `ApiError` on expired/duplicate query ids, retry policy
- [polling-vs-webhook.md](polling-vs-webhook.md) — delivery modes; inline handlers are identical under both
