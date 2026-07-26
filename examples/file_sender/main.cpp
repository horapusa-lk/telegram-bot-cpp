// Demonstrates the four InputFile forms: /photo sends by URL, /doc uploads a
// local file, /buffer uploads generated in-memory bytes, and every received
// photo is echoed back by file_id.

#include <cstdio>
#include <string>

#include <tgbot/tgbot.hpp>

#include "../common.hpp"

int main(int argc, char** argv) {
    tgbot::Bot bot(examples::require_env("TG_BOT_TOKEN"));
    const std::string local_path = argc > 1 ? argv[1] : "README.md";

    bot.onCommand("photo", [&](const tgbot::Message& m) {
        bot.api().sendPhoto({
            .chat_id = m.chat.id,
            .photo = tgbot::InputFile::fromUrl("https://picsum.photos/400"),
            .caption = "fetched by Telegram from a URL",
        });
    });

    bot.onCommand("doc", [&](const tgbot::Message& m) {
        bot.api().sendDocument({
            .chat_id = m.chat.id,
            .document = tgbot::InputFile::fromPath(local_path),
            .caption = "streamed from local disk",
        });
    });

    bot.onCommand("buffer", [&](const tgbot::Message& m) {
        bot.api().sendDocument({
            .chat_id = m.chat.id,
            .document =
                tgbot::InputFile::fromBuffer("hello from memory\n", "greeting.txt", "text/plain"),
            .caption = "uploaded from an in-memory buffer",
        });
    });

    bot.onMessage([&](const tgbot::Message& m) {
        if (m.photo.has_value() && !m.photo->empty()) {
            // Echo the largest size back without re-uploading: file_id reuse.
            bot.api().sendPhoto({
                .chat_id = m.chat.id,
                .photo = tgbot::InputFile::fromFileId(m.photo->back().file_id),
                .caption = "same photo, sent by file_id",
            });
        }
    });

    std::printf("commands: /photo /doc /buffer — or send me a photo\n");
    bot.startPolling();
    return 0;
}
