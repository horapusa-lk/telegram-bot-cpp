# tgbot API reference

A complete C++20 client for the Telegram Bot API 10.2: every method, every
type, long polling and webhooks.

Start here:

- tgbot::Bot — the facade: routing plus either delivery mode.
- tgbot::Api — all 185 methods; each takes one aggregate params struct
  (designated initializers read like the official docs).
- tgbot::InputFile — local path, memory buffer, file_id, or URL.
- tgbot::ApiError / tgbot::RetryPolicy — rich errors, automatic rate-limit
  handling.
- tgbot::Dispatcher, tgbot::LongPoller, tgbot::WebhookServer — the delivery
  pipeline underneath the facade.
- tgbot::InlineKeyboardBuilder / tgbot::ReplyKeyboardBuilder — markup
  builders.

Task-oriented guides (getting started, polling vs webhook with a cloudflared
walkthrough, files, keyboards, inline mode, payments, error handling,
versioning, testing) live in the repository's `docs/` directory, and the
official protocol reference is at https://core.telegram.org/bots/api.
