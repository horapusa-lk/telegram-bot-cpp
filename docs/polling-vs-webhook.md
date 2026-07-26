# Polling vs Webhook

Telegram delivers updates to a bot in one of two ways: the bot pulls them with
[getUpdates](https://core.telegram.org/bots/api#getupdates) (long polling), or
Telegram pushes them to an HTTPS endpoint you register with
[setWebhook](https://core.telegram.org/bots/api#setwebhook). In tgbot-cpp-full
both transports feed the same `tgbot::Dispatcher`, so every handler you
register on `tgbot::Bot` — `onCommand`, `onMessage`, `onCallbackQuery`, and the
rest — works unchanged with either. Switching is a delivery decision, not a
rewrite.

| | Long polling | Webhook |
|---|---|---|
| Setup | None — just a token | Public HTTPS endpoint (proxy or tunnel) |
| Works behind NAT / firewall | Yes | Needs a tunnel (see below) |
| Latency | Up to one poll round-trip | Push, near-instant |
| Concurrency | One batch at a time | Up to `max_connections` parallel deliveries |
| Best for | Development, small bots, restricted networks | Production, high-traffic bots |

A bot can only use one transport at a time: while a webhook is registered,
`getUpdates` fails with a 409 `ApiError` (and vice versa, an active `getUpdates`
poller conflicts with webhook delivery). The switching recipes at the end of
this guide handle that.

## Long polling with `LongPoller`

The simplest path is the `Bot` facade (see also
[getting-started.md](getting-started.md)):

```cpp
#include <cstdio>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onCommand("stop", [&](const tgbot::Message& m) {
        bot.api().sendMessage({.chat_id = m.chat.id, .text = "Bye!"});
        bot.stopPolling();  // safe to call from a handler
    });

    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text.has_value()) {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
        }
    });

    bot.startPolling();  // blocks until stopPolling()
    return 0;
}
```

`startPolling` runs a `tgbot::LongPoller` on the calling thread. You can also
construct one directly when you manage your own `Api` and `Dispatcher`:

```cpp
tgbot::Api api(token);
tgbot::Dispatcher dispatcher;
dispatcher.onUpdate([&](const tgbot::Update& u) { /* ... */ });

tgbot::LongPoller poller(api, dispatcher, {
    .timeout_seconds = 50,
    .allowed_updates = std::vector<std::string>{"message", "callback_query"},
});
poller.run();  // blocks; call poller.stop() from any thread
```

### How the loop works

- **Long poll, not busy poll.** Each iteration calls `getUpdates` with
  `LongPollOptions::timeout_seconds` (default 50). Telegram holds the request
  open until an update arrives or the timeout expires, so an idle bot costs
  roughly one HTTP request per minute.
- **Offset management.** The poller tracks the offset as the highest dispatched
  `update_id + 1` and commits it *after* the batch is handed to the
  dispatcher. If the process crashes mid-batch, the next run re-fetches those
  updates — delivery is at-least-once, never silently dropped. The current
  value is exposed via `poller.offset()` for checkpointing.
- **Error backoff.** A transport failure (`tgbot::NetworkError`) or API error
  does not kill the loop. The poller sleeps `error_backoff` (default 2 s),
  doubling per consecutive failure up to `max_error_backoff` (default 30 s),
  and resets to normal on the first success. Note this is the *poll loop's*
  self-healing; per-request retries of individual API calls are governed by
  `tgbot::RetryPolicy` — see
  [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md).
- **Graceful stop.** `stop()` returns immediately and is safe from any thread,
  including inside a handler. `run()` exits as soon as the in-flight
  `getUpdates` completes; it also interrupts a backoff sleep, so shutdown never
  waits for the full backoff delay.

All tunables live in `tgbot::LongPollOptions`:

```cpp
struct LongPollOptions {
    std::int64_t timeout_seconds = 50;
    std::optional<std::int64_t> limit;                             // batch size, Telegram default 100
    std::optional<std::vector<std::string>> allowed_updates;
    std::chrono::milliseconds error_backoff{2'000};
    std::chrono::milliseconds max_error_backoff{30'000};
};
```

## Webhooks with `WebhookServer`

`tgbot::WebhookServer` is an embedded HTTP server that receives Telegram's
POSTs and feeds them to the dispatcher. Two design points matter:

1. **It speaks plain HTTP.** Telegram requires an HTTPS endpoint, so TLS
   terminates *in front of* the bot — a reverse proxy (nginx, Caddy) or a
   cloudflared tunnel. The server binds `127.0.0.1` by default and never
   touches certificates. This mirrors the library's own integration test
   environment (`TG_WEBHOOK_BIND=127.0.0.1:8591` behind a tunnel — see
   `.env.example`).
2. **Requests are acknowledged fast.** Every valid request gets its HTTP
   response immediately; the update is parsed and dispatched on a worker
   thread pool afterwards. Slow handlers never make Telegram retry.

Configuration is `tgbot::WebhookOptions`:

```cpp
struct WebhookOptions {
    std::string host = "127.0.0.1";  // bind interface
    int port = 8443;                 // 0 picks a free port (see WebhookServer::port())
    std::string path = "/";          // URL path Telegram POSTs to
    std::string secret_token;        // enforced per request when non-empty
    std::size_t worker_threads = 4;  // dispatch pool size
    std::function<std::optional<nlohmann::json>(const Update&)> sync_responder;
};
```

### Secret token verification

When `secret_token` is non-empty, every incoming request must carry a matching
`X-Telegram-Bot-Api-Secret-Token` header; mismatches are rejected with **403**
before any parsing or dispatch. Pass the same value as `.secret_token` in
`setWebhook` and Telegram echoes it on every delivery — this is how you know a
POST really came from Telegram and not a stranger who found your URL. The
token may be 1–256 characters from `A-Z a-z 0-9 _ -`.

### A complete webhook bot

This is `examples/echo_webhook/main.cpp`, the same echo bot delivered over a
webhook:

```cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));
    const std::string public_url = std::getenv("TG_WEBHOOK_URL");  // https://bot.example.com

    bot.onMessage([&](const tgbot::Message& m) {
        if (m.text.has_value()) {
            bot.api().sendMessage({.chat_id = m.chat.id, .text = *m.text});
        }
    });

    tgbot::WebhookOptions options;
    options.host = "127.0.0.1";
    options.port = 8591;
    options.secret_token = "example-secret-8591";
    bot.startWebhook(options);  // serves in the background, throws on bind failure

    bot.api().setWebhook({.url = public_url, .secret_token = options.secret_token});
    std::printf("webhook registered: %s -> 127.0.0.1:8591\n", public_url.c_str());

    for (;;) {  // serve until interrupted (Ctrl+C)
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}
```

`Bot::startWebhook` starts the server on a background thread and returns a
`WebhookServer&`; registering the URL with `setWebhook` is your job (the
library does not guess your public hostname). `Bot::stopWebhook` — or
`WebhookServer::stop()` — stops accepting, drains the worker pool, and joins
all threads. With `port = 0` the OS picks a free port, which you read back
with `server.port()`.

### Answering the webhook directly (`sync_responder`)

Telegram lets a webhook response *carry one API call back* instead of a plain
200, saving a round trip
([making requests when getting updates](https://core.telegram.org/bots/api#making-requests-when-getting-updates)).
Set `WebhookOptions::sync_responder`; it runs inline before the HTTP response.
Return a payload built with `tgbot::answer_webhook_with` to use it, or
`std::nullopt` for a normal 200. Regular handlers still run afterwards on the
worker pool.

```cpp
options.sync_responder = [](const tgbot::Update& u) -> std::optional<nlohmann::json> {
    if (u.message && u.message->text == "ping") {
        return tgbot::answer_webhook_with(
            "sendMessage", {{"chat_id", u.message->chat.id}, {"text", "pong"}});
    }
    return std::nullopt;
};
```

Keep the responder fast — it blocks the HTTP reply — and remember you get no
result object back: the method executes on Telegram's side with no return
channel. Anything that needs the sent `Message` should go through a normal
handler and `api()` instead.

## Exposing the bot with a cloudflared tunnel

A [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/)
gives you a public HTTPS hostname that forwards to `127.0.0.1` — no open
inbound ports, no certificates to manage, works behind NAT. This is exactly
the setup the library's integration tests run against. You need a domain
managed by Cloudflare (any plan, including free).

### 1. Install cloudflared

```sh
# Windows
winget install Cloudflare.cloudflared

# macOS
brew install cloudflared

# Debian/Ubuntu (grab the latest release)
curl -LO https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
sudo dpkg -i cloudflared-linux-amd64.deb
```

### 2. Authenticate and create a tunnel

```sh
cloudflared tunnel login          # opens a browser, pick your zone
cloudflared tunnel create tgbot   # prints a tunnel UUID, writes <UUID>.json credentials
```

### 3. Route a hostname to the tunnel

```sh
cloudflared tunnel route dns tgbot bot.example.com
```

This creates a `bot.example.com` DNS record pointing at the tunnel. Cloudflare
terminates TLS on port 443 at its edge, which is one of the ports Telegram
accepts for webhooks (443, 80, 88, 8443).

### 4. Ingress configuration

Create `~/.cloudflared/config.yml` (`%UserProfile%\.cloudflared\config.yml` on
Windows), forwarding the hostname to the local bind address used throughout
this guide and in `.env.example`:

```yaml
tunnel: tgbot
credentials-file: /home/you/.cloudflared/<TUNNEL-UUID>.json

ingress:
  - hostname: bot.example.com
    service: http://127.0.0.1:8591
  - service: http_status:404
```

The final catch-all rule is required; it answers 404 for anything that is not
your hostname.

### 5. Run it — foreground first, then as a service

```sh
cloudflared tunnel run tgbot
```

With the tunnel up and the bot bound to `127.0.0.1:8591`,
`https://bot.example.com` now reaches your `WebhookServer`. Once it works,
install it as a service so it survives reboots:

```sh
# Linux (systemd)
sudo cloudflared --config ~/.cloudflared/config.yml service install
sudo systemctl start cloudflared

# Windows (run from an elevated prompt)
cloudflared service install
```

### 6. Register, verify, switch back

Register the public URL together with the secret the server enforces:

```cpp
bot.api().setWebhook({
    .url = "https://bot.example.com",
    .secret_token = "my-secret-token",   // A-Z a-z 0-9 _ - only
});
```

Verify delivery with `getWebhookInfo` — the returned `tgbot::WebhookInfo` is
the fastest way to debug a webhook that stays silent:

```cpp
const tgbot::WebhookInfo info = bot.api().getWebhookInfo();
std::printf("url=%s pending=%lld\n", info.url.c_str(),
            static_cast<long long>(info.pending_update_count));
if (info.last_error_message) {
    std::printf("last error: %s\n", info.last_error_message->c_str());
}
```

An empty `info.url` means no webhook is set (the bot is in `getUpdates` mode).
A growing `pending_update_count` plus a `last_error_message` such as
"connection refused" or "SSL error" points at the tunnel or bind address.

To switch back to polling, remove the webhook first — otherwise `getUpdates`
answers 409:

```cpp
bot.api().deleteWebhook({.drop_pending_updates = true});  // or {} to keep the queue
bot.startPolling();
```

`drop_pending_updates` discards everything queued while the webhook was down;
omit it to have the backlog delivered through the first polls instead. Both
`setWebhook` and `deleteWebhook` return `bool` and throw `tgbot::ApiError` on
failure — see [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md)
for handling `error_code()` and `retry_after()`.
