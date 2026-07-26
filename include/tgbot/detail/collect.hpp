#pragma once

/// @file
/// @brief Machinery that gathers multipart uploads out of request params.
///
/// Generated per-type overloads (declared in detail/collect_generated.hpp)
/// walk every struct that can transitively carry a tgbot::InputFile; these
/// generic overloads handle the containers between them.  Only upload forms
/// (path/buffer) produce parts — file_id/URL forms serialize as plain strings.
/// Everything lives in namespace tgbot so the calls resolve via ADL.

#include <optional>
#include <variant>
#include <vector>

#include "tgbot/http_client.hpp"
#include "tgbot/input_file.hpp"

namespace tgbot {

/// Appends the multipart part for an upload-form file; passthrough forms
/// (file_id/URL) contribute nothing.
inline void collect_files(const InputFile& file, std::vector<MultipartPart>& out) {
    if (file.needs_upload()) {
        out.push_back(file.to_part());
    }
}

/// Collects from an engaged optional.
template <typename T>
void collect_files(const std::optional<T>& value, std::vector<MultipartPart>& out) {
    if (value.has_value()) {
        collect_files(*value, out);
    }
}

/// Collects from every element.
template <typename T>
void collect_files(const std::vector<T>& values, std::vector<MultipartPart>& out) {
    for (const T& v : values) {
        collect_files(v, out);
    }
}

/// Collects from the active variant alternative.
template <typename... Ts>
void collect_files(const std::variant<Ts...>& value, std::vector<MultipartPart>& out) {
    std::visit([&out](const auto& alt) { collect_files(alt, out); }, value);
}

/// Catch-all for types that cannot carry files (needed when a variant mixes
/// file-bearing and plain alternatives).  Exact-match generated overloads are
/// always preferred by overload resolution.
template <typename T>
void collect_files(const T& /*value*/, std::vector<MultipartPart>& /*out*/) {}

}  // namespace tgbot
