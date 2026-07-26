#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/input_file.hpp>

using nlohmann::json;
using tgbot::InputFile;

TEST_CASE("file_id and URL forms pass through as strings", "[input_file]") {
    auto by_id = InputFile::fromFileId("AgACAgIAAxk");
    auto by_url = InputFile::fromUrl("https://example.com/cat.png");

    CHECK_FALSE(by_id.needs_upload());
    CHECK_FALSE(by_url.needs_upload());
    CHECK(by_id.reference() == "AgACAgIAAxk");
    CHECK(by_url.reference() == "https://example.com/cat.png");
    CHECK(json(by_id) == "AgACAgIAAxk");
    CHECK(json(by_url) == "https://example.com/cat.png");
}

TEST_CASE("buffer form uploads with unique attach name", "[input_file]") {
    auto a = InputFile::fromBuffer("AAA", "a.txt", "text/plain");
    auto b = InputFile::fromBuffer("BBB", "b.txt");

    CHECK(a.needs_upload());
    CHECK(a.reference().rfind("attach://", 0) == 0);
    CHECK(a.reference() != b.reference());

    auto part = a.to_part();
    CHECK("attach://" + part.name == a.reference());
    CHECK(std::get<std::string>(part.data) == "AAA");
    CHECK(part.filename == "a.txt");
    CHECK(part.content_type == "text/plain");
}

TEST_CASE("path form streams from disk and defaults the filename", "[input_file]") {
    auto f = InputFile::fromPath("photos/cat with space.png");
    CHECK(f.needs_upload());
    auto part = f.to_part();
    CHECK(part.filename == "cat with space.png");
    CHECK(std::get<std::filesystem::path>(part.data) ==
          std::filesystem::path("photos/cat with space.png"));
}

TEST_CASE("from_json preserves the string verbatim for round-trips", "[input_file]") {
    const json j = "https://example.com/dog.gif";
    auto f = j.get<InputFile>();
    CHECK(json(f) == j);
}
