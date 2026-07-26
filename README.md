# telegram-bot-cpp

[![CI](https://github.com/horapusa-lk/telegram-bot-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/horapusa-lk/telegram-bot-cpp/actions/workflows/ci.yml)
[![Docs](https://github.com/horapusa-lk/telegram-bot-cpp/actions/workflows/docs.yml/badge.svg)](https://horapusa-lk.github.io/telegram-bot-cpp/)
[![Bot API](https://img.shields.io/badge/Bot%20API-10.2-blue)](https://core.telegram.org/bots/api)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A complete, generated-from-the-source-of-truth C++20 client for the
**Telegram Bot API 10.2** — every method, every type, both delivery modes.

- **100% API coverage, provably.** All 388 object types and 185 methods are
  generated from a scraped, validated inventory of the official reference
  ([spec/api_inventory.json](spec/api_inventory.json)); a coverage test diffs
  the inventory against the compiled symbols, so nothing can silently go
  missing. `tgbot::api_version()` returns `"10.2"`.
- **Typed end to end.** `std::optional` for optional fields, `std::variant`
  for unions (`ChatMember`, `ReactionType`, `MessageOrigin`, …), value
  semantics with deep-copying `tgbot::Box` for the recursive corners,
  aggregate params structs with designated initializers that read like the
  official docs.
- **Both update modes, one handler interface.** `getUpdates` long polling
  (offset management, graceful shutdown, self-healing backoff) and an
  embedded webhook server (secret-token verification, worker pool,
  answer-webhook-with-method) feed the same `Dispatcher`.
- **Files done right.** `tgbot::InputFile` covers local paths, in-memory
  buffers, `file_id` reuse and URLs; requests promote to
  `multipart/form-data` automatically, even for media nested inside
  `sendMediaGroup`.
- **Rich errors & rate limits.** `tgbot::ApiError` carries `error_code`,
  `description`, `retry_after`, `migrate_to_chat_id`; HTTP 429 is honored
  automatically and transient faults retry with exponential backoff
  (configurable `tgbot::RetryPolicy`).

## Install

### CMake FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(tgbot
    GIT_REPOSITORY https://github.com/horapusa-lk/telegram-bot-cpp.git
    GIT_TAG main)
FetchContent_MakeAvailable(tgbot)

add_executable(mybot main.cpp)
target_link_libraries(mybot PRIVATE tgbot::tgbot)
```

### System install

With `nlohmann_json` and `CURL` development packages installed:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTGBOT_INSTALL=ON
cmake --build build
cmake --install build
```

then `find_package(tgbot REQUIRED)` and link `tgbot::tgbot`.

## 30-second quick start — long polling

```cpp
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));
    bot.onCommand("start", [&](const tgbot::Message& m) {
        bot.api().sendMessage({.chat_id = m.chat.id, .text = "Hello!"});
    });
    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text) bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
    });
    bot.startPolling();  // blocks; Ctrl+C or bot.stopPolling() to exit
}
```

## 30-second quick start — webhook

```cpp
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));
    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text) bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
    });

    // Plain HTTP locally; TLS terminates at your tunnel/reverse proxy.
    bot.startWebhook({.host = "127.0.0.1", .port = 8591, .secret_token = "change-me"});
    bot.api().setWebhook({.url = std::getenv("TG_WEBHOOK_URL"),
                          .secret_token = "change-me"});
    for (;;) std::this_thread::sleep_for(std::chrono::minutes(1));
}
```

Handler code is identical in both modes — see
[docs/polling-vs-webhook.md](docs/polling-vs-webhook.md) for a full
cloudflared tunnel walkthrough.

## Documentation

- **[API reference (Doxygen)](https://horapusa-lk.github.io/telegram-bot-cpp/)** — every public symbol
- [Getting started](docs/getting-started.md)
- [Polling vs webhook (+ cloudflared setup)](docs/polling-vs-webhook.md)
- [Sending files](docs/sending-files.md)
- [Keyboards & callbacks](docs/keyboards-and-callbacks.md)
- [Inline mode](docs/inline-mode.md)
- [Payments & Telegram Stars](docs/payments-and-stars.md)
- [Error handling & rate limits](docs/error-handling-and-rate-limits.md)
- [Versioning & migration policy](docs/versioning-policy.md)
- [Testing (unit + live integration)](docs/testing.md)
- API reference: `cmake -DTGBOT_BUILD_DOCS=ON` + `cmake --build build --target docs`
  (Doxygen; undocumented public symbols fail the build)

## Examples

[`examples/`](examples/) builds with `-DTGBOT_BUILD_EXAMPLES=ON` (default when
top-level): `echo_polling`, `echo_webhook`, `inline_keyboard_menu`,
`file_sender`, `inline_query_bot`.

## Bot API version bumps

The whole surface regenerates mechanically:

```bash
python tools/scrape_api.py --fetch     # re-scrape the official reference
python tools/validate_inventory.py     # integrity + golden assertions
python tools/generate.py               # re-emit types, methods, tests
cmake --build build && ctest --test-dir build
```

The coverage tests fail until every new type and method is implemented —
which the generator does automatically for anything non-structural.

## Contributing & license

See [CONTRIBUTING.md](CONTRIBUTING.md) (most of the code is generated — edit
the generator, not its output) and [SECURITY.md](SECURITY.md).
Licensed under the [MIT License](LICENSE).
