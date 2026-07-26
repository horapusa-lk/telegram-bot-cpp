#pragma once

/// @file
/// @brief Serialization helpers shared by the generated JSON glue.

#include <optional>

#include <nlohmann/json.hpp>

#include "tgbot/box.hpp"

namespace tgbot {

/// Serializes the boxed value transparently.
template <typename T>
void to_json(nlohmann::json& j, const Box<T>& box) {
    j = *box;
}

/// Deserializes into the boxed value transparently.
template <typename T>
void from_json(const nlohmann::json& j, Box<T>& box) {
    j.get_to(*box);
}

namespace detail {

/// Writes a required field.
template <typename T>
void write(nlohmann::json& j, const char* key, const T& value) {
    j[key] = value;
}

/// Writes an optional field only when it holds a value.
template <typename T>
void write(nlohmann::json& j, const char* key, const std::optional<T>& value) {
    if (value.has_value()) {
        write(j, key, *value);
    }
}

/// Reads a required field; throws nlohmann's out_of_range when missing.
template <typename T>
void read(const nlohmann::json& j, const char* key, T& out) {
    j.at(key).get_to(out);
}

/// Reads an optional field; absent or null leaves it empty.
template <typename T>
void read(const nlohmann::json& j, const char* key, std::optional<T>& out) {
    if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
        it->get_to(out.emplace());
    } else {
        out.reset();
    }
}

}  // namespace detail
}  // namespace tgbot
