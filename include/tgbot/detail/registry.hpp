#pragma once

/// @file
/// @brief Registry of implemented API symbols, used by the coverage gate.

#include <string_view>
#include <vector>

namespace tgbot::detail {

/// Names of every Bot API object type whose serializers the generated code
/// implements.  Each entry is compile-time proven: the generated registry
/// takes the address of the actual @c to_json / @c from_json symbols.
const std::vector<std::string_view>& implemented_types();

/// Names of every Bot API method tgbot::Api implements; entries are proven by
/// taking the address of the member function.
const std::vector<std::string_view>& implemented_methods();

}  // namespace tgbot::detail
