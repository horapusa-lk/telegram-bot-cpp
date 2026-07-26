# Keyboards and Callbacks

Telegram bots have two keyboard families:

- **Inline keyboards** ([`InlineKeyboardMarkup`](https://core.telegram.org/bots/api#inlinekeyboardmarkup)) — buttons attached to a specific message. Pressing one either opens a URL or sends your bot a [`CallbackQuery`](https://core.telegram.org/bots/api#callbackquery).
- **Reply keyboards** ([`ReplyKeyboardMarkup`](https://core.telegram.org/bots/api#replykeyboardmarkup)) — buttons that replace the user's system keyboard. Pressing one sends an ordinary text message.

Both are passed through the `reply_markup` field present on every send method, alongside two control markups for reply keyboards: [`ReplyKeyboardRemove`](https://core.telegram.org/bots/api#replykeyboardremove) and [`ForceReply`](https://core.telegram.org/bots/api#forcereply).

## The `ReplyMarkup` variant

Send-method params structs (`SendMessageParams`, `SendPhotoParams`, and the rest) declare:

```cpp
/// Additional interface options. A JSON-serialized object for an inline keyboard, custom reply
/// keyboard, instructions to remove a reply keyboard or to force a reply from the user.
std::optional<ReplyMarkup> reply_markup;
```

where `ReplyMarkup` is a union of the four markup types:

```cpp
/// Any of the four keyboard-control markups accepted by send methods.
using ReplyMarkup = std::variant<InlineKeyboardMarkup, ReplyKeyboardMarkup,
                                 ReplyKeyboardRemove, ForceReply>;
```

Any of the four alternatives converts implicitly, so you can assign a builder's `build()` result — or a brace-initialized `ReplyKeyboardRemove{}` — straight into `reply_markup` inside a designated initializer. No wrapping needed.

## Building inline keyboards

`tgbot::InlineKeyboardBuilder` (from `include/tgbot/keyboards.hpp`) is a fluent builder. Buttons accumulate left-to-right; `row()` starts the next row; `build()` produces an `InlineKeyboardMarkup` and drops empty rows.

| Method | Button it adds |
| --- | --- |
| `text(label, callback_data)` | Callback button — sends `callback_data` (1–64 bytes) to your bot |
| `url(label, target_url)` | Opens `target_url` |
| `webApp(label, web_app_url)` | Launches a [Web App](https://core.telegram.org/bots/webapps) |
| `switchInline(label, query)` | Prompts the user to pick a chat and inserts an inline query there (see [inline-mode.md](inline-mode.md)) |
| `button(InlineKeyboardButton)` | Any fully custom button |
| `row()` | Starts a new row |
| `build()` | Returns the `InlineKeyboardMarkup` |

```cpp
#include <cstdlib>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onCommand("menu", [&](const tgbot::Message& m) {
        bot.api().sendMessage({
            .chat_id = m.chat.id,
            .text = "Pick a flavor:",
            .reply_markup =
                tgbot::InlineKeyboardBuilder()
                    .text("Vanilla", "flavor:vanilla")
                    .text("Chocolate", "flavor:chocolate")
                    .row()
                    .url("More flavors", "https://en.wikipedia.org/wiki/Ice_cream")
                    .build(),
        });
    });

    bot.startPolling();
    return 0;
}
```

A common convention (used throughout the examples) is to namespace `callback_data` with a prefix like `"flavor:"` so one handler can route several keyboards.

### Custom inline buttons

The shorthand methods cover the common cases; for everything else construct a `tgbot::InlineKeyboardButton` yourself and pass it to `button()`. The struct exposes the full Bot API surface — a selection of its `std::optional` fields:

```cpp
tgbot::InlineKeyboardButton b;
b.text = "Copy coupon code";
b.copy_text.emplace();
b.copy_text->text = "SAVE20";

auto markup = tgbot::InlineKeyboardBuilder()
                  .button(std::move(b))
                  .row()
                  .text("Done", "coupon:done")
                  .build();
```

Other fields you may reach for: `switch_inline_query_current_chat`, `switch_inline_query_chosen_chat`, `login_url` (a [`LoginUrl`](https://core.telegram.org/bots/api#loginurl) for seamless authorization), `callback_game`, `pay` (invoice messages — see [payments-and-stars.md](payments-and-stars.md)), and `style` (`"danger"`, `"success"`, or `"primary"`).

## Handling callback queries

Register a handler with `Bot::onCallbackQuery`; it receives the `tgbot::CallbackQuery`:

```cpp
struct CallbackQuery {
    std::string id;                                     // answer with this
    User from;                                          // who pressed the button
    std::optional<MaybeInaccessibleMessage> message;    // the message the keyboard is on
    std::optional<std::string> inline_message_id;       // set for inline-mode messages instead
    std::string chat_instance;
    std::optional<std::string> data;                    // the button's callback_data
    std::optional<std::string> game_short_name;
};
```

Two rules apply to every handler:

1. **Always call `answerCallbackQuery`**, even with no text — until you do, the user's client shows a loading spinner on the button (Telegram gives up after ~30 seconds, which looks broken).
2. **`q.message` is a variant, not a `Message`.** Its type is `MaybeInaccessibleMessage = std::variant<Message, InaccessibleMessage>`: if the message with the keyboard is too old (over 48 hours) or otherwise inaccessible to the bot, you get an [`InaccessibleMessage`](https://core.telegram.org/bots/api#inaccessiblemessage) stub instead. Use `std::get_if<tgbot::Message>` before touching chat or message ids.

This is the pattern from `examples/inline_keyboard_menu/main.cpp`:

```cpp
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
```

### Answer options

`AnswerCallbackQueryParams` supports more than a silent acknowledgement:

```cpp
bot.api().answerCallbackQuery({
    .callback_query_id = q.id,
    .text = "Out of stock, sorry!",  // toast at the top of the chat (0-200 chars)
    .show_alert = true,             // modal alert dialog instead of a toast
    .cache_time = 60,               // client may cache the result for 60 s
});
```

There is also `.url` for deep links like `t.me/your_bot?start=XXXX` (or opening a Game). `answerCallbackQuery` returns `bool` and, like every `Api` method, throws `tgbot::ApiError` on failure — see [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md).

### Editing the message in place

Menus feel responsive when a tap edits the existing message rather than sending a new one. The `editMessage*` family targets a message either by `chat_id` + `message_id`, or — for messages sent via inline mode — by `inline_message_id` alone:

```cpp
// Swap just the keyboard, keeping the text.
bot.api().editMessageReplyMarkup({
    .chat_id = accessible->chat.id,
    .message_id = accessible->message_id,
    .reply_markup = tgbot::InlineKeyboardBuilder()
                        .text("Back", "menu:root")
                        .build(),
});
```

Notes on the edit methods:

- `reply_markup` on `EditMessageTextParams` / `EditMessageReplyMarkupParams` is `std::optional<InlineKeyboardMarkup>` — only inline keyboards can be edited onto a message; the full `ReplyMarkup` variant exists only on send methods.
- They return `MessageOrBool` (`std::variant<Message, bool>`): the edited `Message` for a normal message, or `true` when the target was an inline message.
- Editing a message to identical text and markup fails with an `ApiError` (`400: message is not modified`), so track state or catch that case.

## Building reply keyboards

`tgbot::ReplyKeyboardBuilder` mirrors the inline builder, plus setters for the markup-level options of [`ReplyKeyboardMarkup`](https://core.telegram.org/bots/api#replykeyboardmarkup):

| Method | Effect |
| --- | --- |
| `text(label)` | Plain button — pressing it sends `label` as a message |
| `requestContact(label)` | Asks the user to share their phone number |
| `requestLocation(label)` | Asks the user to share their location |
| `button(KeyboardButton)` | Any fully custom button |
| `row()` | Starts a new row |
| `resize(bool = true)` | Sets `resize_keyboard` — shrink the keyboard vertically |
| `oneTime(bool = true)` | Sets `one_time_keyboard` — hide after one use |
| `selective(bool = true)` | Sets `selective` — show only to targeted users |
| `placeholder(text)` | Sets `input_field_placeholder` (shown in the input field) |
| `persistent(bool = true)` | Sets `is_persistent` — always shown |
| `build()` | Returns the `ReplyKeyboardMarkup` |

```cpp
#include <cstdlib>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onCommand("start", [&](const tgbot::Message& m) {
        bot.api().sendMessage({
            .chat_id = m.chat.id,
            .text = "What next?",
            .reply_markup = tgbot::ReplyKeyboardBuilder()
                                .text("Help").text("About")
                                .row()
                                .requestLocation("Share location")
                                .resize()
                                .placeholder("Choose an option")
                                .build(),
        });
    });

    // Reply-keyboard presses arrive as ordinary text messages.
    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text == "Help") {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = "No."});
        }
    });

    bot.startPolling();
    return 0;
}
```

There is no callback involved: a reply-keyboard press is just a text message carrying the button label, so route it in `onMessage` (or make the labels commands and use `onCommand`).

For richer buttons, build a `tgbot::KeyboardButton` and pass it to `button()`. Its optional fields include `request_users` / `request_chat` (user- and chat-picker buttons that report back via service messages), `request_poll`, `web_app`, and `style`.

## Removing a keyboard and forcing a reply

The last two `ReplyMarkup` alternatives are plain structs whose discriminating field already defaults to `true`, so `{}` is a complete value:

```cpp
// Take the custom keyboard away (ReplyKeyboardRemove{.remove_keyboard = true}).
bot.api().sendMessage({
    .chat_id = chat_id,
    .text = "Thanks, that's everything!",
    .reply_markup = tgbot::ReplyKeyboardRemove{},
});

// Open the reply interface, as if the user tapped Reply on this message.
bot.api().sendMessage({
    .chat_id = chat_id,
    .text = "What should the item be called?",
    .reply_markup = tgbot::ForceReply{
        .input_field_placeholder = "Item name",
    },
});
```

Both structs also carry `std::optional<bool> selective` to target only users \@mentioned in the text (or the sender of the message being replied to). `ForceReply` is handy for step-by-step input flows without disabling [privacy mode](https://core.telegram.org/bots/features#privacy-mode); the user's next message arrives as a reply to yours, so you can match it via `reply_to_message`.

## See also

- [getting-started.md](getting-started.md) — bot setup, handlers, and polling basics
- [inline-mode.md](inline-mode.md) — `switchInline` buttons land users in inline mode
- [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md) — `ApiError`, retry-after, and retry policy
