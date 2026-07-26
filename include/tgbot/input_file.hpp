#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "tgbot/http_client.hpp"

namespace tgbot {

/// @brief A file to send through the Bot API.
///
/// Covers all four ways the API accepts files:
///  - fromPath(): upload a local file via @c multipart/form-data;
///  - fromBuffer(): upload in-memory bytes via @c multipart/form-data;
///  - fromFileId(): reuse a file already on Telegram's servers;
///  - fromUrl(): let Telegram fetch the file from a public HTTP(S) URL.
///
/// Upload forms serialize as an <tt>attach://\<name\></tt> reference and the
/// request is automatically promoted to @c multipart/form-data; passthrough
/// forms serialize as the plain string.  Fields documented as "InputFile or
/// String" are represented by this class as well.
///
/// @see https://core.telegram.org/bots/api#inputfile
class InputFile {
public:
    /// How the file content is provided.
    enum class Kind {
        path,     ///< local file streamed from disk
        buffer,   ///< in-memory bytes
        file_id,  ///< identifier of a file already on Telegram servers
        url,      ///< public HTTP(S) URL fetched by Telegram
    };

    /// Equivalent to fromFileId() with an empty id; present so the type can be
    /// used in aggregate params structs, but not meaningful to send.
    InputFile() = default;

    /// Uploads the local file at @p path.  The remote file name defaults to
    /// the path's file name; @p content_type is optional.
    static InputFile fromPath(std::filesystem::path path, std::string content_type = {},
                              std::string filename = {});

    /// Uploads @p bytes (arbitrary binary data) under @p filename.
    static InputFile fromBuffer(std::string bytes, std::string filename,
                                std::string content_type = {});

    /// Reuses a file already stored on Telegram's servers.
    static InputFile fromFileId(std::string file_id);

    /// Asks Telegram to fetch the file from @p url.
    static InputFile fromUrl(std::string url);

    /// True for path/buffer forms, which require a multipart upload.
    bool needs_upload() const noexcept { return kind_ == Kind::path || kind_ == Kind::buffer; }

    /// Which of the four forms this instance is.
    Kind kind() const noexcept { return kind_; }

    /// The string this file serializes to in JSON: the file_id or URL for
    /// passthrough forms, or an <tt>attach://\<name\></tt> reference for
    /// uploads.
    std::string reference() const;

    /// Builds the multipart part carrying the content of an upload form.
    /// @pre needs_upload()
    MultipartPart to_part() const;

private:
    Kind kind_ = Kind::file_id;
    std::filesystem::path path_;
    std::string data_;  ///< buffer bytes, file_id, or URL depending on kind_
    std::string filename_;
    std::string content_type_;
    std::string attach_name_;  ///< unique multipart name for upload forms
};

/// Serializes as reference(): plain string or @c attach://<name>.
void to_json(nlohmann::json& j, const InputFile& file);

/// Reconstructs a passthrough InputFile from a string (upload forms cannot be
/// round-tripped from JSON; the string is preserved verbatim).
void from_json(const nlohmann::json& j, InputFile& file);

}  // namespace tgbot
