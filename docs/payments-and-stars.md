# Payments & Telegram Stars

Telegram bots accept payments in two ways: [Telegram Stars](https://core.telegram.org/bots/api#payments) (currency code `XTR`, mandatory for digital goods and services) and fiat currencies through a payment provider connected via @BotFather. The library covers the full surface: `Api::sendInvoice`, `Api::createInvoiceLink`, `Api::answerShippingQuery`, `Api::answerPreCheckoutQuery`, `Api::refundStarPayment`, `Api::getMyStarBalance`, `Api::getStarTransactions`, and `Api::editUserStarSubscription`.

The flow is always the same three steps:

1. You send an invoice (`sendInvoice`) or hand out an invoice link (`createInvoiceLink`).
2. Telegram sends your bot a `pre_checkout_query` update (and, for flexible-price fiat invoices, a `shipping_query` first). You must answer it — within 10 seconds for pre-checkout.
3. On success, the chat receives a service `Message` whose `successful_payment` field carries the charge ids you store for refunds and support.

## Sending a Stars invoice

For Stars, set `currency` to `"XTR"`, leave `provider_token` unset (it is a `std::optional<std::string>`; no provider is involved), and pass exactly one `LabeledPrice` whose `amount` is the price in whole Stars:

```cpp
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    bot.onCommand("buy", [&](const tgbot::Message& m) {
        bot.api().sendInvoice({
            .chat_id = m.chat.id,
            .title = "Pro subscription",
            .description = "30 days of premium features",
            .payload = "pro-30d",  // internal id, never shown to the user
            .currency = "XTR",
            .prices = {{.label = "Pro (30 days)", .amount = 250}},  // 250 Stars
        });
    });

    bot.startPolling();
    return 0;
}
```

Key `SendInvoiceParams` fields (they mirror [sendInvoice](https://core.telegram.org/bots/api#sendinvoice) in order, so designated initializers read like the official docs):

| Field | Type | Notes |
| --- | --- | --- |
| `chat_id` | `ChatId` (`std::variant<std::int64_t, std::string>`) | Target chat |
| `title`, `description` | `std::string` | 1–32 / 1–255 characters |
| `payload` | `std::string` | 1–128 bytes, echoed back in `PreCheckoutQuery::invoice_payload` and `SuccessfulPayment::invoice_payload` |
| `provider_token` | `std::optional<std::string>` | Omit (or pass `""`) for Stars; the @BotFather provider token for fiat |
| `currency` | `std::string` | `"XTR"` for Stars, otherwise an ISO 4217 code |
| `prices` | `std::vector<LabeledPrice>` | Exactly one item for Stars |
| `reply_markup` | `std::optional<InlineKeyboardMarkup>` | If empty, Telegram shows a default "Pay total price" button |

`LabeledPrice` is just `{ std::string label; std::int64_t amount; }`. For fiat currencies `amount` is in the smallest currency unit (cents for USD); for `XTR` it is the number of Stars.

The fiat-only knobs — `max_tip_amount`, `suggested_tip_amounts`, `need_name`, `need_phone_number`, `need_email`, `need_shipping_address`, `send_phone_number_to_provider`, `send_email_to_provider`, `is_flexible`, `provider_data` — are all `std::optional` and ignored (or rejected) for Stars invoices, so simply leave them out.

### A custom Pay button

If you attach your own keyboard, the first button of the first row must be a Pay button — an `InlineKeyboardButton` with `pay = true`. `InlineKeyboardBuilder` (see [keyboards-and-callbacks.md](keyboards-and-callbacks.md)) has no dedicated pay helper, so build the markup directly; the substrings `⭐` and `XTR` in the button text are replaced with a Star icon:

```cpp
tgbot::InlineKeyboardMarkup markup{
    .inline_keyboard = {
        {tgbot::InlineKeyboardButton{.text = "Pay 250 XTR", .pay = true}},
        {tgbot::InlineKeyboardButton{.text = "Terms", .url = "https://example.com/terms"}},
    },
};

bot.api().sendInvoice({
    .chat_id = m.chat.id,
    .title = "Pro subscription",
    .description = "30 days of premium features",
    .payload = "pro-30d",
    .currency = "XTR",
    .prices = {{.label = "Pro (30 days)", .amount = 250}},
    .reply_markup = markup,
});
```

## Invoice links

[`createInvoiceLink`](https://core.telegram.org/bots/api#createinvoicelink) takes the same pricing fields but no `chat_id` and returns a `https://t.me/...` URL you can put behind a button, a website, or a QR code:

```cpp
std::string url = bot.api().createInvoiceLink({
    .title = "Pro subscription",
    .description = "30 days of premium features",
    .payload = "pro-30d",
    .currency = "XTR",
    .prices = {{.label = "Pro (30 days)", .amount = 250}},
});
```

`CreateInvoiceLinkParams` adds two link-only fields: `business_connection_id` (create the link on behalf of a connected business account, Stars only) and `subscription_period` — set it to `2592000` (30 days, currently the only allowed value) to turn the link into a recurring Stars subscription. Subscription price is capped at 10000 Stars.

## Answering the checkout queries

Between "user pressed Pay" and "money moved", Telegram asks your bot for confirmation. These arrive as `Update` fields, not as `Message`s, so the `Bot` facade routes them through `onAnyUpdate` — the catch-all that sees every `tgbot::Update` before command/message routing:

```cpp
bot.onAnyUpdate([&](const tgbot::Update& u) {
    // Fiat invoices with is_flexible = true only: quote shipping options.
    if (u.shipping_query) {
        const tgbot::ShippingQuery& q = *u.shipping_query;
        if (q.shipping_address.country_code == "US") {
            bot.api().answerShippingQuery({
                .shipping_query_id = q.id,
                .ok = true,
                .shipping_options = {{
                    {.id = "std", .title = "Standard", .prices = {{.label = "Shipping", .amount = 599}}},
                    {.id = "exp", .title = "Express", .prices = {{.label = "Shipping", .amount = 1499}}},
                }},
            });
        } else {
            bot.api().answerShippingQuery({
                .shipping_query_id = q.id,
                .ok = false,
                .error_message = "Sorry, we only ship to the US.",
            });
        }
    }

    // Every payment, Stars included: final go/no-go. Answer within 10 seconds.
    if (u.pre_checkout_query) {
        const tgbot::PreCheckoutQuery& q = *u.pre_checkout_query;
        const bool in_stock = (q.invoice_payload == "pro-30d");
        bot.api().answerPreCheckoutQuery({
            .pre_checkout_query_id = q.id,
            .ok = in_stock,
            .error_message = in_stock ? std::optional<std::string>{}
                                      : "This product is no longer available.",
        });
    }
});
```

`PreCheckoutQuery` gives you everything needed for the decision: `id`, `from` (the paying `User`), `currency`, `total_amount`, `invoice_payload`, plus optional `shipping_option_id` and `order_info`. If the bot does not answer within 10 seconds, the payment fails on the user's side.

Shipping queries only occur for fiat invoices sent with `need_shipping_address = true` and `is_flexible = true`; Stars payments never produce them.

> Note: if you restrict update types via `LongPollOptions::allowed_updates` (or the equivalent `setWebhook` parameter), include `"pre_checkout_query"` and `"shipping_query"` in the list, or the queries will never reach your bot.

## Handling the successful payment

After a positive `answerPreCheckoutQuery`, Telegram completes the charge and delivers a service message with `Message::successful_payment` set. It is a regular message, so `onMessage` sees it:

```cpp
bot.onMessage([&](const tgbot::Message& m) {
    if (!m.successful_payment) {
        return;
    }
    const tgbot::SuccessfulPayment& p = *m.successful_payment;

    // Persist this id: it is required for refunds and support requests.
    store_order(p.invoice_payload, p.telegram_payment_charge_id,
                m.from ? m.from->id : 0);

    bot.api().sendMessage({
        .chat_id = m.chat.id,
        .text = "Payment received — thank you! Your order: " + p.invoice_payload,
    });
});
```

`SuccessfulPayment` carries `currency`, `total_amount`, `invoice_payload`, `telegram_payment_charge_id`, `provider_payment_charge_id`, and — for Stars subscriptions — `subscription_expiration_date`, `is_recurring`, and `is_first_recurring` (all `std::optional`).

## Refunding a Stars payment

[`refundStarPayment`](https://core.telegram.org/bots/api#refundstarpayment) refunds a successful Stars payment in full. You need the user id and the stored `telegram_payment_charge_id`:

```cpp
bool ok = bot.api().refundStarPayment({
    .user_id = payer_id,
    .telegram_payment_charge_id = charge_id,
});
```

The user receives a service message about the refund (`Message::refunded_payment`), and the refund shows up in your transaction history as an outgoing `StarTransaction` whose `id` coincides with the original one.

## Balance and transaction history

`getMyStarBalance()` takes no parameters and returns a `StarAmount` — `amount` (whole Stars, can be negative) plus an optional `nanostar_amount` in billionths:

```cpp
tgbot::StarAmount balance = bot.api().getMyStarBalance();
std::printf("bot balance: %lld Stars\n", static_cast<long long>(balance.amount));
```

`getStarTransactions` returns a `StarTransactions` (a `std::vector<StarTransaction> transactions`) and paginates with `offset`/`limit` (1–100, default 100):

```cpp
tgbot::StarTransactions page = bot.api().getStarTransactions({.offset = 0, .limit = 50});
```

Each `StarTransaction` has an `id`, `amount`, optional `nanostar_amount`, `date`, and either a `source` (incoming) or a `receiver` (outgoing). Both are `std::optional<TransactionPartner>`, and `TransactionPartner` is a `std::variant` over `TransactionPartnerUser`, `TransactionPartnerChat`, `TransactionPartnerAffiliateProgram`, `TransactionPartnerFragment`, `TransactionPartnerTelegramAds`, `TransactionPartnerTelegramApi`, and `TransactionPartnerOther` — use `std::get_if` or `std::visit` as with every union in the library:

```cpp
for (const tgbot::StarTransaction& t : page.transactions) {
    if (!t.source) {
        continue;  // outgoing (refund, withdrawal, ...)
    }
    if (const auto* u = std::get_if<tgbot::TransactionPartnerUser>(&*t.source)) {
        std::printf("+%lld Stars from user %lld (payload: %s)\n",
                    static_cast<long long>(t.amount),
                    static_cast<long long>(u->user.id),
                    u->invoice_payload.value_or("-").c_str());
    }
}
```

`TransactionPartnerUser::transaction_type` distinguishes `"invoice_payment"`, `"paid_media_payment"`, `"gift_purchase"`, `"premium_purchase"`, and `"business_account_transfer"`.

## Stars subscriptions

A subscription starts from an invoice link created with `subscription_period = 2592000`. Every renewal produces a fresh `pre_checkout_query` → `successful_payment` cycle; check `is_recurring`/`is_first_recurring` and the new `subscription_expiration_date` on each `SuccessfulPayment`.

To cancel (or un-cancel) a user's subscription from the bot side, use [`editUserStarSubscription`](https://core.telegram.org/bots/api#edituserstarsubscription) with the charge id of the subscription payment:

```cpp
// Stop future renewals; access stays until the current period ends.
bot.api().editUserStarSubscription({
    .user_id = payer_id,
    .telegram_payment_charge_id = charge_id,
    .is_canceled = true,
});
```

Pass `is_canceled = false` to let a user re-enable a subscription the bot previously canceled. Telegram also delivers a `BotSubscriptionUpdated` update in `Update::subscription` when a user's subscription state changes.

## Errors

All of these methods throw `tgbot::ApiError` on a Telegram-side rejection (bad currency, unknown charge id, insufficient balance for a refund, ...) — inspect `error_code()` and `description()` — and `tgbot::NetworkError` on transport failure. See [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md).

## Testing note

Payment methods move real Stars and real money, so they are deliberately **excluded from the live integration test suite**. They are covered by unit tests against a mock transport instead — every method above is exercised in `tests/unit/generated/method_smoke_corpus.cpp`, and the payment types round-trip through JSON in the generated corpus tests. See [testing.md](testing.md) for how the mock-transport tests work and how to structure your own handler tests without touching the live API.
