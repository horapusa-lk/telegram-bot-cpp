#include "tgbot/api_error.hpp"

#include <utility>

namespace tgbot {

namespace {

std::string make_what(int error_code, const std::string& description) {
    return "Telegram Bot API error " + std::to_string(error_code) + ": " + description;
}

}  // namespace

ApiError::ApiError(int error_code, std::string description, std::optional<int> retry_after,
                   std::optional<std::int64_t> migrate_to_chat_id)
    : Error(make_what(error_code, description)),
      error_code_(error_code),
      description_(std::move(description)),
      retry_after_(retry_after),
      migrate_to_chat_id_(migrate_to_chat_id) {}

}  // namespace tgbot
