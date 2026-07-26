# Getting Started

tgbot-cpp-full is a complete C++20 client library for the
[Telegram Bot API](https://core.telegram.org/bots/api), covering every type and
method of API 10.2. This guide takes you from an empty directory to a running
echo bot.

## Requirements

- A C++20 compiler (MSVC 19.3x, GCC 11+, or Clang 14+)
- CMake **3.20 or newer**
- Network access on first configure — missing dependencies are downloaded
  automatically via `FetchContent`

The library depends on [nlohmann/json](https://github.com/nlohmann/json) (3.11+)
and [libcurl](https://curl.se/libcurl/). The build first tries
`find_package` for both and falls back to fetching and building them in-tree,
so no system packages are strictly required. The embedded webhook server uses
cpp-httplib internally; it is always fetched and never exposed to consumers.

## Adding the library to your project

### Option A: FetchContent (recommended)

Embed the library directly into your build. A complete downstream
`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(echo_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(tgbot
    GIT_REPOSITORY https://github.com/your-org/tgbot-cpp-full.git
    GIT_TAG        main)   # pin a release tag in real projects
FetchContent_MakeAvailable(tgbot)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE tgbot::tgbot)
```

That is all: `tgbot::tgbot` carries the include paths, the C++20 requirement,
and the JSON/curl link dependencies transitively. When tgbot is consumed as a
subproject like this, its tests and examples are off by default (they only
build when tgbot is the top-level project).

### Option B: `find_package` after installing

If you prefer an installed package, build and install tgbot once:

```sh
cmake -S tg-bot-cpp -B build -DCMAKE_BUILD_TYPE=Release \
      -DTGBOT_INSTALL=ON -DTGBOT_BUILD_TESTS=OFF -DTGBOT_BUILD_EXAMPLES=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"   # or /usr/local, C:/dev/prefix, ...
```

One caveat: `TGBOT_INSTALL` requires **nlohmann_json and CURL to be installed
packages** discoverable by `find_package` (e.g. from vcpkg, apt, brew). When
either one was fetched in-tree instead, install/export rules are disabled —
CMake cannot export a target that references another project's in-tree
targets. The option defaults to `ON` exactly when both packages were found.

Consuming the installed package:

```cmake
cmake_minimum_required(VERSION 3.20)
project(echo_bot LANGUAGES CXX)

find_package(tgbot 0.1 REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE tgbot::tgbot)
```

Point CMake at your prefix if it is non-standard:
`cmake -B build -DCMAKE_PREFIX_PATH=$HOME/.local`. The installed
`tgbotConfig.cmake` re-finds nlohmann_json and CURL via `find_dependency()`,
so they must be visible to the consumer as well.

### Build options

All options live in the top-level `CMakeLists.txt`:

| Option                  | Default                                    | Effect                                              |
| ----------------------- | ------------------------------------------ | --------------------------------------------------- |
| `TGBOT_BUILD_TESTS`     | `ON` when top-level, else `OFF`            | Build unit and integration tests (`ctest`)          |
| `TGBOT_BUILD_EXAMPLES`  | `ON` when top-level, else `OFF`            | Build the example programs in `examples/`           |
| `TGBOT_BUILD_DOCS`      | `OFF`                                      | Add the Doxygen `docs` target                       |
| `TGBOT_SANITIZERS`      | `OFF`                                      | Build with Address/UB sanitizers (non-MSVC)         |
| `TGBOT_INSTALL`         | `ON` when nlohmann_json + CURL were found  | Generate install/export rules (see caveat above)    |

## Creating a bot with @BotFather

Every bot needs a token issued by Telegram:

1. Open Telegram and message [@BotFather](https://t.me/BotFather).
2. Send `/newbot`, pick a display name, then a username ending in `bot`
   (e.g. `my_echo_bot`).
3. BotFather replies with a token of the form `123456:ABC-DEF1234...`.

Treat the token like a password — never commit it. The repository's
`.env.example` shows the environment contract used by the examples and
integration tests; copy it to `.env` (git-ignored) and set:

```sh
TG_BOT_TOKEN=123456:ABC-your-token-here
```

## Your first bot: echo over long polling

Create `main.cpp` next to the `CMakeLists.txt` from Option A:

```cpp
#include <cstdio>
#include <cstdlib>
#include <string>

#include <tgbot/tgbot.hpp>

int main() {
    const char* token = std::getenv("TG_BOT_TOKEN");
    if (token == nullptr || *token == '\0') {
        std::fprintf(stderr, "error: set TG_BOT_TOKEN first\n");
        return 1;
    }

    tgbot::Bot bot(token);

    // "/start" (and "/start@YourBot") — command matching is case-insensitive.
    bot.onCommand("start", [&](const tgbot::Message& m) {
        bot.api().sendMessage({
            .chat_id = m.chat.id,
            .text = "Hello! Send me anything and I will echo it.",
        });
    });

    // Any message not consumed by a command handler.
    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text.has_value()) {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
        }
    });

    // Exceptions thrown by handlers land here instead of killing the loop.
    bot.onError([](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "handler error: %s\n", e.what());
        }
    });

    const tgbot::User me = bot.api().getMe();
    std::printf("polling as @%s (Bot API %s)\n",
                me.username.value_or("?").c_str(),
                std::string(tgbot::api_version()).c_str());

    bot.startPolling();  // blocks until bot.stopPolling()
    return 0;
}
```

Build and run:

```sh
cmake -B build && cmake --build build
TG_BOT_TOKEN=123456:ABC-your-token-here ./build/app
```

Open your bot's chat in Telegram, send `/start`, then any text — it comes
straight back.

A few things worth noticing in that snippet:

- **One params struct per method.** Every API call takes a single aggregate
  filled with designated initializers:
  `api.sendMessage({.chat_id = ..., .text = ...})`. Optional Telegram fields
  are `std::optional` members you simply leave out — add
  `.parse_mode = "MarkdownV2"` to the initializer when you need it.
- **`bot.api()`** exposes the full typed [`tgbot::Api`](../include/tgbot/api.hpp)
  surface — all 185 methods, not just what the facade routes.
- **Routing order matters.** A message that matches an `onCommand` handler is
  consumed; plain `onMessage` handlers do not see it.
- **`startPolling()`** runs [getUpdates](https://core.telegram.org/bots/api#getupdates)
  long polling on the calling thread. It accepts a `tgbot::LongPollOptions`
  aggregate (`timeout_seconds`, `limit`, `allowed_updates`, error backoff
  tunables) if the defaults do not fit, and `stopPolling()` is safe to call
  from inside a handler.

The same program ships as a buildable example in
[`examples/echo_polling/main.cpp`](../examples/echo_polling/main.cpp);
configure the repository top-level and the binaries land in
`build/examples/`.

## Where to go next

- [polling-vs-webhook.md](polling-vs-webhook.md) — when to switch from
  `startPolling()` to `startWebhook()`, with a cloudflared tunnel walkthrough.
- [keyboards-and-callbacks.md](keyboards-and-callbacks.md) — inline and reply
  keyboards with `InlineKeyboardBuilder` / `ReplyKeyboardBuilder`, and handling
  button presses via `onCallbackQuery`.
- [sending-files.md](sending-files.md) — photos, documents, and media groups
  with `tgbot::InputFile::fromPath/fromBuffer/fromFileId/fromUrl`.
- [inline-mode.md](inline-mode.md) — answering `onInlineQuery` requests.
- [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md) —
  `tgbot::ApiError` / `tgbot::NetworkError`, and automatic 429 retries via
  `tgbot::RetryPolicy`.
- [testing.md](testing.md) — running the unit and integration test suites.
