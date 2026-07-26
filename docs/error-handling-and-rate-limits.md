# Error Handling & Rate Limits

Everything tgbot-cpp-full throws derives from a single base class, and most transient
failures — network hiccups, HTTP 5xx, and Telegram's 429 rate limits — are retried for
you before an exception ever reaches your code. This guide covers the exception
hierarchy, the `RetryPolicy` knobs, what to do when a rate limit still surfaces, and
how to broadcast to many chats without tripping Telegram's limits.

## The exception hierarchy

Defined in `tgbot/api_error.hpp`:

```
std::runtime_error
└── tgbot::Error              — base of all library exceptions
    ├── tgbot::ApiError       — Telegram answered with ok=false
    └── tgbot::NetworkError   — transport failed: DNS, TLS, reset, timeout,
                                or a response that is not a Bot API envelope
```

Catch `tgbot::Error` when you only care that *something* went wrong; catch the two
subclasses when you want to distinguish "Telegram rejected the call" from "the call
never got a valid answer".

### `ApiError`

An `ApiError` carries the fields of the Bot API error envelope, including the optional
[`ResponseParameters`](https://core.telegram.org/bots/api#responseparameters) object:

| Accessor | Type | Meaning |
|---|---|---|
| `error_code()` | `int` | Numeric code from the envelope (400, 403, 429, ...) |
| `description()` | `const std::string&` | Human-readable explanation |
| `retry_after()` | `std::optional<int>` | `ResponseParameters.retry_after` — seconds to wait (rate limits) |
| `migrate_to_chat_id()` | `std::optional<std::int64_t>` | `ResponseParameters.migrate_to_chat_id` — the group became a supergroup |

A typical send with targeted handling:

```cpp
#include <cstdio>
#include <tgbot/tgbot.hpp>

void notify(tgbot::Api& api, std::int64_t chat_id, const std::string& text) {
    try {
        api.sendMessage({.chat_id = chat_id, .text = text});
    } catch (const tgbot::ApiError& e) {
        if (e.migrate_to_chat_id()) {
            // The group was upgraded to a supergroup: resend to the new id
            // (and persist it — the old id is dead).
            api.sendMessage({.chat_id = *e.migrate_to_chat_id(), .text = text});
        } else if (e.error_code() == 403) {
            std::printf("user %lld blocked the bot\n", (long long)chat_id);
        } else {
            std::printf("API error %d: %s\n", e.error_code(), e.description().c_str());
        }
    } catch (const tgbot::NetworkError& e) {
        // Transport failed even after the retry budget was spent.
        std::printf("network failure: %s\n", e.what());
    }
}
```

`what()` on an `ApiError` includes the error code, so logging the exception as a plain
`std::exception` still tells you what happened. The bot token never appears in error
messages.

## Automatic retries: `RetryPolicy`

Every call made through `tgbot::Api` (and the raw `ApiClient::call`) runs under a
`RetryPolicy`, configured on `ApiClient::Options` (`tgbot/api_client.hpp`). Two classes
of failure are retried:

- **Rate limiting** — HTTP 429 with `retry_after`: the client sleeps the
  server-requested duration, then repeats the call.
- **Transient faults** — `NetworkError` and HTTP 5xx: retried with exponential
  backoff starting at `initial_backoff`.

Every other Bot API error (400 bad request, 403 forbidden, ...) is thrown immediately
as `ApiError` — retrying those would never help.

The defaults and knobs:

| Field | Default | Meaning |
|---|---|---|
| `max_attempts` | `3` | Total attempts per call (`1` = no retries) |
| `initial_backoff` | `500ms` | First delay for transient faults |
| `backoff_multiplier` | `2.0` | Factor applied to the backoff after each failed attempt |
| `max_retry_after` | `600s` | Rate-limit waits longer than this give up immediately |
| `retry_on_rate_limit` | `true` | Honor 429 `retry_after` by sleeping and retrying |
| `retry_on_server_error` | `true` | Retry HTTP 5xx (Bot API hiccups, proxy errors) |
| `retry_on_network_error` | `true` | Retry `tgbot::NetworkError` failures |

Tune it when constructing the bot — `Bot`, `Api`, and `ApiClient` all accept
`ApiClient::Options`:

```cpp
#include <chrono>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::ApiClient::Options options;
    options.retry.max_attempts = 5;
    options.retry.initial_backoff = std::chrono::milliseconds(250);
    options.retry.max_retry_after = std::chrono::seconds(30);
    options.request_timeout = std::chrono::milliseconds(30'000);

    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"), options);
    // ...
}
```

### When a 429 still reaches you

With the defaults, most rate limits are invisible: the client sleeps `retry_after`
seconds and the call eventually succeeds. You will still see `ApiError` with
`error_code() == 429` when:

- the retry budget is exhausted — `max_attempts` calls all came back 429;
- Telegram asks for a wait longer than `max_retry_after` — the client refuses to
  block that long and throws immediately, with `retry_after()` set so you can decide
  (persist the work and come back later, shed load, ...);
- you disabled the behavior with `retry_on_rate_limit = false`.

In all three cases `retry_after()` is populated — honor it before repeating the call:

```cpp
try {
    bot.api().sendMessage({.chat_id = chat_id, .text = text});
} catch (const tgbot::ApiError& e) {
    if (e.error_code() == 429 && e.retry_after()) {
        std::this_thread::sleep_for(std::chrono::seconds(*e.retry_after()));
        bot.api().sendMessage({.chat_id = chat_id, .text = text});  // one more try
    } else {
        throw;
    }
}
```

## Handler errors: `Bot::onError`

An exception thrown inside a handler never tears down the delivery loop — under both
polling and webhooks it is routed to the error sink registered with `Bot::onError`
(or silently swallowed when none is set, so always register one):

```cpp
#include <cstdio>
#include <exception>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onMessage([&](const tgbot::Message& m) {
        // Anything thrown here lands in onError, not on the floor.
        bot.api().sendMessage({.chat_id = m.chat.id, .text = m.text.value_or("?")});
    });

    bot.onError([](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const tgbot::ApiError& e) {
            std::fprintf(stderr, "API error %d: %s\n", e.error_code(), e.description().c_str());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "handler error: %s\n", e.what());
        }
    });

    bot.startPolling();
}
```

The sink receives a `std::exception_ptr` (`tgbot::ErrorHandler` is
`std::function<void(std::exception_ptr)>`), so the rethrow-and-catch pattern above is
the way to inspect it. If you use the `Dispatcher` directly instead of the `Bot`
facade, the same hook is `Dispatcher::onError`; the webhook server additionally routes
request payloads that failed to parse there.

Failures of the polling loop itself (a `getUpdates` call failing beyond its retry
budget) do not stop the bot either: `LongPoller` retries with its own capped
exponential backoff (`LongPollOptions::error_backoff`, doubling up to
`max_error_backoff`, reset on success) — see [polling-vs-webhook.md](polling-vs-webhook.md).

## Broadcasting to many chats

Telegram enforces per-bot sending limits (roughly one message per second per chat and
around 30 messages per second overall — see the
[Bots FAQ](https://core.telegram.org/bots/faq#my-bot-is-hitting-limits-how-do-i-avoid-this)).
The retry machinery will absorb occasional 429s, but a tight send loop turns into a
sleep-retry crawl. For a clean broadcast:

- **Spread the sends** — pace the loop yourself instead of relying on retries:

```cpp
#include <chrono>
#include <thread>
#include <vector>
#include <tgbot/tgbot.hpp>

void broadcast(tgbot::Api& api, const std::vector<std::int64_t>& chat_ids,
               const std::string& text) {
    for (std::int64_t chat_id : chat_ids) {
        try {
            api.sendMessage({.chat_id = chat_id, .text = text});
        } catch (const tgbot::ApiError& e) {
            if (e.error_code() == 403) continue;   // user blocked the bot: drop them
            if (e.error_code() == 429 && e.retry_after()) {
                std::this_thread::sleep_for(std::chrono::seconds(*e.retry_after()));
                continue;                          // resume; optionally re-queue chat_id
            }
            throw;
        }
        // ~25 msg/s, safely under the global limit.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}
```

- **Treat 403 as unsubscribe** — remove chats that blocked the bot from your list so
  you stop paying for dead sends.
- **Pay to go faster** — send methods take an `allow_paid_broadcast` field
  (e.g. `SendMessageParams::allow_paid_broadcast`, a `std::optional<bool>`): pass
  `true` to allow up to 1000 messages per second, ignoring broadcast limits, for a fee
  of 0.1 Telegram Stars per message withdrawn from the bot's balance:

```cpp
api.sendMessage({
    .chat_id = chat_id,
    .text = text,
    .allow_paid_broadcast = true,
});
```

For the basics of setting up a bot see [getting-started.md](getting-started.md); for
upload-specific failure modes see [sending-files.md](sending-files.md).
