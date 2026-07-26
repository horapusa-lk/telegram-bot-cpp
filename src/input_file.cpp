#include "tgbot/input_file.hpp"

#include <atomic>
#include <utility>

#include <nlohmann/json.hpp>

namespace tgbot {

namespace {

// Each upload form gets a process-unique attach:// name so any number of
// files can coexist inside one multipart request.
std::string next_attach_name() {
    static std::atomic<unsigned long long> counter{0};
    return "file" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

}  // namespace

InputFile InputFile::fromPath(std::filesystem::path path, std::string content_type,
                              std::string filename) {
    InputFile f;
    f.kind_ = Kind::path;
    if (filename.empty()) {
        filename = path.filename().string();
    }
    f.path_ = std::move(path);
    f.filename_ = std::move(filename);
    f.content_type_ = std::move(content_type);
    f.attach_name_ = next_attach_name();
    return f;
}

InputFile InputFile::fromBuffer(std::string bytes, std::string filename, std::string content_type) {
    InputFile f;
    f.kind_ = Kind::buffer;
    f.data_ = std::move(bytes);
    f.filename_ = std::move(filename);
    f.content_type_ = std::move(content_type);
    f.attach_name_ = next_attach_name();
    return f;
}

InputFile InputFile::fromFileId(std::string file_id) {
    InputFile f;
    f.kind_ = Kind::file_id;
    f.data_ = std::move(file_id);
    return f;
}

InputFile InputFile::fromUrl(std::string url) {
    InputFile f;
    f.kind_ = Kind::url;
    f.data_ = std::move(url);
    return f;
}

std::string InputFile::reference() const {
    if (needs_upload()) {
        return "attach://" + attach_name_;
    }
    return data_;
}

MultipartPart InputFile::to_part() const {
    MultipartPart part;
    part.name = attach_name_;
    part.filename = filename_;
    part.content_type = content_type_;
    if (kind_ == Kind::path) {
        part.data = path_;
    } else {
        part.data = data_;
    }
    return part;
}

void to_json(nlohmann::json& j, const InputFile& file) {
    j = file.reference();
}

void from_json(const nlohmann::json& j, InputFile& file) {
    file = InputFile::fromFileId(j.get<std::string>());
}

}  // namespace tgbot
