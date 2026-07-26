# Testing

tgbot-cpp-full ships two tiers of tests:

- **Unit tests** — hermetic, no network, no token. The HTTP transport is a scripted
  mock, and two generated corpora exercise every one of the 388 Bot API types and
  185 methods in `spec/api_inventory.json`. Always on; CTest label `unit`.
- **Integration tests** — run against the real Bot API with your own bot token and a
  public webhook tunnel. Off by default; CTest label `integration`.

A coverage gate ties the whole suite to the Bot API 10.2 inventory: if the spec
gains a type or method the generated code does not implement, the unit suite fails.

## Suite layout

```
tests/
├── CMakeLists.txt              builds tgbot_unit_tests (Catch2), discovers cases
├── unit/                       hermetic — transport mocked, no credentials
│   ├── mock_http_client.hpp    scripted HttpClient test double
│   ├── api_client_test.cpp     envelope parsing, retries, 429/5xx, multipart
│   ├── api_request_test.cpp    exact JSON bodies, attach:// wiring, unions
│   ├── bot_test.cpp            command routing, keyboard builders
│   ├── box_test.cpp            Box<T> value semantics for recursive types
│   ├── coverage_test.cpp       coverage gate against spec/api_inventory.json
│   ├── dispatcher_test.cpp     handler order, error hook
│   ├── error_test.cpp          ApiError / NetworkError hierarchy
│   ├── input_file_test.cpp     fromPath / fromBuffer / fromFileId / fromUrl
│   ├── long_polling_test.cpp   offset advancement, backoff, stop()
│   ├── webhook_test.cpp        real localhost HTTP: secret token, 403/400/404
│   ├── roundtrip_test.cpp      driver for the generated type corpus
│   ├── method_smoke_test.cpp   driver for the generated method corpus
│   └── generated/              corpora emitted by tools/generate.py
└── integration/                live tests, label "integration" (opt-in)
```

## The mocked transport

Unit tests never touch the network. `tgbot::HttpClient` is an abstract interface,
and `tgbot::ApiClient::Options` accepts any implementation via its `http` field.
The test double, `tgbot::test::MockHttpClient` in `tests/unit/mock_http_client.hpp`,
records every outgoing `HttpRequest` and replays a scripted sequence of responses
(the last script entry repeats forever; `enqueue_network_error()` throws
`tgbot::NetworkError` instead).

A typical unit test primes the mock, calls the typed API, then asserts on both the
parsed result and the exact request that was built:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/tgbot.hpp>

#include "mock_http_client.hpp"

TEST_CASE("sendMessage builds the documented JSON body") {
    auto mock = std::make_shared<tgbot::test::MockHttpClient>();
    mock->enqueue(200,
        R"({"ok":true,"result":{"message_id":1,"date":7,)"
        R"("chat":{"id":5,"type":"private"}}})");

    tgbot::ApiClient::Options options;
    options.http = mock;
    const tgbot::Api api(tgbot::ApiClient("TEST_TOKEN", options));

    tgbot::Message sent = api.sendMessage({
        .chat_id = std::int64_t{5},
        .text = "hello",
        .parse_mode = "MarkdownV2",
    });

    CHECK(sent.message_id == 1);
    REQUIRE(mock->requests.size() == 1);
    CHECK(mock->requests[0].url ==
          "https://api.telegram.org/botTEST_TOKEN/sendMessage");
    const auto body = nlohmann::json::parse(mock->requests[0].body);
    CHECK(body["parse_mode"] == "MarkdownV2");
}
```

The same double drives the retry-policy tests (`api_client_test.cpp` scripts a 429
with `retry_after`, then a success, and asserts the sleep/retry behavior of
`tgbot::RetryPolicy`) and the multipart tests (`api_request_test.cpp` checks that an
`InputFile::fromBuffer` upload promotes the request to `multipart/form-data` with
`attach://` references). `webhook_test.cpp` is the one unit file that opens a real
socket — it starts `tgbot::WebhookServer` on `127.0.0.1` with an ephemeral port and
POSTs to it locally, so it still needs no Telegram connectivity.

## Generated corpora

`tools/generate.py` emits two test corpora into `tests/unit/generated/` alongside
the library code, so the tests can never drift from the implementation:

- **Round-trip corpus** — one synthetic JSON sample per type, all 388 types, split
  across four shards (`roundtrip_corpus_0.cpp` … `roundtrip_corpus_3.cpp`). Each
  `RoundtripCase` parses its sample into the C++ struct, serializes it back, and
  requires the two JSON values to compare equal — proving every field, every
  `std::optional`, and every `std::variant` arm survives a full parse/serialize
  cycle. Set the environment variable `TGBOT_TRACE_ROUNDTRIP=1` to print each type
  name as it runs, which pinpoints a crash inside the corpus.
- **Method smoke corpus** — one `MethodSmokeCase` per method, all 185 methods
  (`method_smoke_corpus.cpp`). Each case invokes the real `tgbot::Api` member with
  default params against the mock transport primed with a minimal typed result,
  then asserts the request URL ends in `/<methodName>` and that the JSON body (or
  multipart form) is well-formed and the result parses back into the typed return
  value without throwing.

### The coverage gate

`unit/coverage_test.cpp` reads `spec/api_inventory.json` (path baked in via the
`TGBOT_SPEC_PATH` compile definition) and diffs it against the generated registry:

```cpp
const std::vector<std::string_view>& tgbot::detail::implemented_types();
const std::vector<std::string_view>& tgbot::detail::implemented_methods();
```

Registry entries are compile-time proven — the generated registry takes the
addresses of the real `to_json`/`from_json` functions and `Api` member functions —
so a green gate means every name in the inventory exists as real, linkable code.
If this test fails after the inventory is updated for a new Bot API release,
re-run `tools/generate.py`. See [versioning-policy.md](versioning-policy.md) for
how inventory updates are versioned.

## Running the tests

Tests build by default when tgbot is the top-level project
(`TGBOT_BUILD_TESTS=ON`):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

Notes:

- `ctest -L unit` selects by label; a bare `ctest --test-dir build` runs everything
  that is enabled (which is the same set unless integration tests are switched on).
- With a multi-config generator (Visual Studio), add the configuration:
  `ctest --test-dir build -C Debug -L unit`.
- Catch2 test discovery registers every `TEST_CASE` as its own CTest entry, so
  failures are reported per case. You can also run the binary directly and filter
  by tag: `./build/tests/tgbot_unit_tests "[roundtrip]"` (or `"[api]"`,
  `"[webhook]"`, `"[coverage]"`, …).
- Configure with `-DTGBOT_SANITIZERS=ON` (non-MSVC) to build the library and tests
  with Address/UB sanitizers.

## Integration tests

The integration suite talks to the real Bot API as your bot. It is **off by
default** and lives in `tests/integration/` under the CTest label `integration`.
Two switches must both be thrown:

1. Configure with `-DTGBOT_ENABLE_INTEGRATION_TESTS=ON`.
2. Provide the environment described by `.env.example` — copy it to `.env`
   (git-ignored; the integration runner loads it, and nothing in the repo
   hard-codes credentials):

```sh
# Bot token from @BotFather
TG_BOT_TOKEN=123456:ABC-your-token-here

# Public HTTPS endpoint that forwards to the local webhook bind below
# (e.g. a cloudflared tunnel hostname). Used by setWebhook.
TG_WEBHOOK_URL=https://your-tunnel-hostname.example.com

# Local address the embedded webhook server binds to (host:port).
TG_WEBHOOK_BIND=127.0.0.1:8591

# Optional: chat id used for live sendMessage round-trips. When unset, the
# integration tests try to discover a chat from the bot's pending updates.
#TG_TEST_CHAT_ID=123456789
```

Then:

```sh
cmake -S . -B build -DTGBOT_ENABLE_INTEGRATION_TESTS=ON
cmake --build build
ctest --test-dir build -L integration --output-on-failure
```

Setting up the tunnel that makes `TG_WEBHOOK_URL` reachable is covered in
[polling-vs-webhook.md](polling-vs-webhook.md).

### What each live test does

- **`getMe`** — the smallest possible live call: verifies the token
  authenticates and that [getMe](https://core.telegram.org/bots/api#getme)
  returns the bot's own `User` with `is_bot == true`.
- **`sendMessage` round-trip** — needs a chat the bot may post to. If
  `TG_TEST_CHAT_ID` is set, that chat is used; otherwise the test discovers one
  from the bot's pending updates (`getUpdates` — send your bot any message
  beforehand so there is something to discover). It then calls
  [sendMessage](https://core.telegram.org/bots/api#sendmessage) and asserts the
  returned `Message` echoes the text and chat id.
- **Webhook flow** — binds the embedded `WebhookServer` on `TG_WEBHOOK_BIND`,
  calls [setWebhook](https://core.telegram.org/bots/api#setwebhook) pointing at
  `TG_WEBHOOK_URL` with a freshly generated `secret_token`, pushes an update
  through the public tunnel endpoint, and asserts it is dispatched to the
  handlers while a request with a wrong secret is rejected with 403. Finally it
  calls `deleteWebhook` so the bot is left in polling-capable state.

Use a dedicated test bot for this: the webhook test reconfigures the bot's
delivery mode while it runs, and the discovery step consumes pending updates.

## Unit-only by design

Some API areas are deliberately **never** exercised live, because they cannot run
safely or idempotently against a real account. They are still fully covered by the
generated round-trip and method-smoke corpora plus request-construction unit tests
— what is skipped is only the live call:

| Area | Methods (examples) | Why not live |
| --- | --- | --- |
| Payments | `sendInvoice`, `answerPreCheckoutQuery`, `refundStarPayment` | Moves real money/Telegram Stars; a refund or charge cannot be undone or repeated idempotently. |
| Passport | `setPassportDataErrors` | Requires a real user to have submitted Telegram Passport data; flags errors on real personal documents. |
| Business account mutation | `setBusinessAccountName`, `transferGift` | Requires a paid business account connected to the bot; mutations rename/transfer things on a real account. |
| Stickers mutation | `createNewStickerSet`, `addStickerToSet`, `deleteStickerFromSet` | Creates persistent, globally named state tied to the account; not idempotent and aggressively rate-limited. |
| Game scores | `setGameScore`, `getGameHighScores` | Writes permanent high-score records for real users; scores can only be increased, never reset. |

If you are building on these areas, the unit patterns above are the template:
prime `MockHttpClient` with the documented result payload and assert on the
request body — see [payments-and-stars.md](payments-and-stars.md) for the
payments flow itself and
[error-handling-and-rate-limits.md](error-handling-and-rate-limits.md) for
testing failure paths with scripted `ApiError` envelopes.
