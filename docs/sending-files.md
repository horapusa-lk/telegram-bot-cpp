# Sending Files

Every Bot API field documented as "InputFile or String" is represented in this library by a single type: `tgbot::InputFile` (see [`include/tgbot/input_file.hpp`](../include/tgbot/input_file.hpp)). It covers all four ways the [Telegram Bot API accepts files](https://core.telegram.org/bots/api#sending-files), and the library decides automatically whether a request can stay plain JSON or must become a `multipart/form-data` upload.

## The four forms

```cpp
static InputFile fromPath(std::filesystem::path path, std::string content_type = {},
                          std::string filename = {});
static InputFile fromBuffer(std::string bytes, std::string filename,
                            std::string content_type = {});
static InputFile fromFileId(std::string file_id);
static InputFile fromUrl(std::string url);
```

| Factory | What it does | Upload? |
|---|---|---|
| `InputFile::fromPath(p)` | Streams a local file from disk. Remote filename defaults to the path's filename. | yes |
| `InputFile::fromBuffer(bytes, name)` | Uploads in-memory bytes under the given filename. | yes |
| `InputFile::fromFileId(id)` | Reuses a file already stored on Telegram's servers — no bytes transferred. | no |
| `InputFile::fromUrl(url)` | Telegram fetches the file from a public HTTP(S) URL itself. | no |

`needs_upload()` returns `true` for the path/buffer forms; `kind()` tells you which of the four forms an instance is. Prefer `fromFileId` whenever you can — it is instant and does not count against upload limits.

## Automatic multipart promotion

You never deal with multipart encoding yourself. When the library serializes a params struct:

- passthrough forms (`fromFileId`, `fromUrl`) serialize as their plain string;
- upload forms (`fromPath`, `fromBuffer`) serialize as an `attach://<name>` reference, and the request is automatically promoted from a JSON POST to `multipart/form-data`, with one file part per upload.

This works at any nesting depth. The `media`, `thumbnail`, and `cover` fields inside `InputMediaPhoto`, `InputMediaVideo`, `InputMediaDocument`, etc. are themselves `tgbot::InputFile`, so an upload buried inside a `sendMediaGroup` album is found, given a unique `attach://` name, and attached to the same request — exactly as the official docs describe for [`attach://<file_attach_name>`](https://core.telegram.org/bots/api#inputmediaphoto).

## sendPhoto, sendDocument, sendVideo

A complete bot exercising all four forms (adapted from [`examples/file_sender/main.cpp`](../examples/file_sender/main.cpp)):

```cpp
#include <cstdlib>
#include <tgbot/tgbot.hpp>

int main() {
    tgbot::Bot bot(std::getenv("TG_BOT_TOKEN"));

    // fromUrl: Telegram downloads it, your bot uploads nothing.
    bot.onCommand("photo", [&](const tgbot::Message& m) {
        bot.api().sendPhoto({
            .chat_id = m.chat.id,
            .photo = tgbot::InputFile::fromUrl("https://picsum.photos/400"),
            .caption = "fetched by Telegram from a URL",
        });
    });

    // fromPath: streamed from disk as multipart/form-data.
    bot.onCommand("doc", [&](const tgbot::Message& m) {
        bot.api().sendDocument({
            .chat_id = m.chat.id,
            .document = tgbot::InputFile::fromPath("report.pdf", "application/pdf"),
            .caption = "streamed from local disk",
        });
    });

    // fromBuffer: bytes you generated at runtime, no temp file needed.
    bot.onCommand("csv", [&](const tgbot::Message& m) {
        std::string csv = "id,name\n1,alice\n2,bob\n";
        bot.api().sendDocument({
            .chat_id = m.chat.id,
            .document = tgbot::InputFile::fromBuffer(std::move(csv),
                                                     "users.csv", "text/csv"),
        });
    });

    bot.startPolling();
}
```

`sendVideo` works the same way; its optional `thumbnail` (and `cover`) fields are also `InputFile`, so you can upload a custom thumbnail in the same request:

```cpp
bot.api().sendVideo({
    .chat_id = m.chat.id,
    .video = tgbot::InputFile::fromPath("clip.mp4", "video/mp4"),
    .thumbnail = tgbot::InputFile::fromPath("thumb.jpg", "image/jpeg"),
    .caption = "with a custom thumbnail",
    .supports_streaming = true,
});
```

Per the [official docs](https://core.telegram.org/bots/api#sendvideo), thumbnails must be JPEG, under 200 kB, at most 320 px per side, and cannot be reused by `file_id` — they are always a fresh upload.

## Albums: sendMediaGroup

`SendMediaGroupParams::media` is a `std::vector<tgbot::InputMedia>`, where `InputMedia` is a `std::variant` over `InputMediaPhoto`, `InputMediaVideo`, `InputMediaDocument`, and the other album-capable types. Each element's `media` field is an `InputFile`, and uploads mixed with `file_id` reuse are fine in one call:

```cpp
std::vector<tgbot::Message> sent = bot.api().sendMediaGroup({
    .chat_id = m.chat.id,
    .media = {
        tgbot::InputMediaPhoto{
            .media = tgbot::InputFile::fromPath("first.jpg"),
            .caption = "fresh upload",
        },
        tgbot::InputMediaPhoto{
            .media = tgbot::InputFile::fromFileId(known_file_id),
        },
        tgbot::InputMediaVideo{
            .media = tgbot::InputFile::fromUrl("https://example.com/clip.mp4"),
        },
    },
});
```

An album must contain [2–10 items](https://core.telegram.org/bots/api#sendmediagroup); documents and audio can only be grouped with items of the same type.

## Reusing file_id from received messages

Every file your bot receives (or sends) carries a `file_id` you can resend without re-uploading. Incoming photos arrive as `m.photo` — a `std::optional<std::vector<PhotoSize>>` sorted smallest to largest — and documents as `m.document`:

```cpp
bot.onMessage([&](const tgbot::Message& m) {
    if (m.photo && !m.photo->empty()) {
        // Echo the largest size back: no bytes are re-uploaded.
        bot.api().sendPhoto({
            .chat_id = m.chat.id,
            .photo = tgbot::InputFile::fromFileId(m.photo->back().file_id),
            .caption = "same photo, sent by file_id",
        });
    }
    if (m.document) {
        bot.api().sendDocument({
            .chat_id = m.chat.id,
            .document = tgbot::InputFile::fromFileId(m.document->file_id),
        });
    }
});
```

The `tgbot::Message` returned by every send method contains the `file_id` Telegram assigned, so a common pattern is: upload once at startup, cache the `file_id`, and serve all subsequent requests from it. Note that a `file_id` is only valid for the bot that obtained it; `file_unique_id` is stable across bots but cannot be used for sending or downloading.

## Downloading files: getFile

`Api::getFile()` takes a `GetFileParams` (a single `file_id` field) and returns a `tgbot::File` whose optional `file_path` plugs into the download URL `https://api.telegram.org/file/bot<token>/<file_path>`:

```cpp
bot.onMessage([&](const tgbot::Message& m) {
    if (!m.document) return;

    tgbot::File file = bot.api().getFile({.file_id = m.document->file_id});
    if (file.file_path) {
        const auto& opts = bot.api().client().options();
        std::string url = opts.base_url + "/file/bot" +
                          bot.api().client().token() + "/" + *file.file_path;
        // Fetch `url` with the HTTP client of your choice.
    }
});
```

Building the URL from `client().options().base_url` (default `"https://api.telegram.org"`) keeps the code working against a [Local Bot API Server](https://core.telegram.org/bots/api#using-a-local-bot-api-server), which you select by setting `ApiClient::Options::base_url` when constructing the `Bot`. The link is guaranteed valid for at least one hour; call `getFile` again when it expires. Keep the URL out of logs — it embeds your bot token.

## Size limits

From the [official documentation](https://core.telegram.org/bots/api#sending-files):

| Operation | Limit |
|---|---|
| Upload a photo (`fromPath`/`fromBuffer`) | 10 MB, width + height ≤ 10000, ratio ≤ 20 |
| Upload any other file | 50 MB |
| Send by URL (`fromUrl`) — photos | 5 MB |
| Send by URL (`fromUrl`) — other files | 20 MB |
| Download via `getFile` | 20 MB |
| Send by `file_id` | no limit (the file is already on Telegram's servers) |

A [Local Bot API Server](https://core.telegram.org/bots/api#using-a-local-bot-api-server) raises uploads to 2000 MB and removes the download limit.

Failed sends throw `tgbot::ApiError` (for example when a file exceeds a limit above) or `tgbot::NetworkError` if the upload's transport fails — see [error-handling-and-rate-limits.md](error-handling-and-rate-limits.md). For the basics of constructing the bot and handlers used here, see [getting-started.md](getting-started.md).
