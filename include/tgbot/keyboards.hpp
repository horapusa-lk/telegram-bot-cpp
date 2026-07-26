#pragma once

/// @file
/// @brief Fluent builders for the two keyboard markups.

#include <string>
#include <utility>
#include <vector>

#include "tgbot/types.hpp"

namespace tgbot {

/// @brief Fluent builder for tgbot::InlineKeyboardMarkup.
///
/// Buttons accumulate left-to-right; row() starts the next row.
/// @code
/// auto markup = tgbot::InlineKeyboardBuilder()
///                   .text("Yes", "vote:yes")
///                   .text("No", "vote:no")
///                   .row()
///                   .url("Docs", "https://example.com")
///                   .build();
/// @endcode
class InlineKeyboardBuilder {
public:
    /// Adds a callback button sending @p callback_data to the bot.
    InlineKeyboardBuilder& text(std::string label, std::string callback_data) {
        InlineKeyboardButton b;
        b.text = std::move(label);
        b.callback_data = std::move(callback_data);
        return button(std::move(b));
    }

    /// Adds a button opening @p target_url.
    InlineKeyboardBuilder& url(std::string label, std::string target_url) {
        InlineKeyboardButton b;
        b.text = std::move(label);
        b.url = std::move(target_url);
        return button(std::move(b));
    }

    /// Adds a button launching a Web App.
    InlineKeyboardBuilder& webApp(std::string label, std::string web_app_url) {
        InlineKeyboardButton b;
        b.text = std::move(label);
        b.web_app.emplace();
        b.web_app->url = std::move(web_app_url);
        return button(std::move(b));
    }

    /// Adds a button that inserts an inline query in another chat.
    InlineKeyboardBuilder& switchInline(std::string label, std::string query) {
        InlineKeyboardButton b;
        b.text = std::move(label);
        b.switch_inline_query = std::move(query);
        return button(std::move(b));
    }

    /// Adds a fully custom button.
    InlineKeyboardBuilder& button(InlineKeyboardButton b) {
        if (rows_.empty()) {
            rows_.emplace_back();
        }
        rows_.back().push_back(std::move(b));
        return *this;
    }

    /// Starts a new row; empty rows are dropped by build().
    InlineKeyboardBuilder& row() {
        rows_.emplace_back();
        return *this;
    }

    /// Produces the markup (usable directly as a @c reply_markup value).
    InlineKeyboardMarkup build() const {
        InlineKeyboardMarkup markup;
        for (const auto& row : rows_) {
            if (!row.empty()) {
                markup.inline_keyboard.push_back(row);
            }
        }
        return markup;
    }

private:
    std::vector<std::vector<InlineKeyboardButton>> rows_;
};

/// @brief Fluent builder for tgbot::ReplyKeyboardMarkup.
///
/// @code
/// auto markup = tgbot::ReplyKeyboardBuilder()
///                   .text("Help").text("About").row()
///                   .requestLocation("Share location")
///                   .resize()
///                   .build();
/// @endcode
class ReplyKeyboardBuilder {
public:
    /// Adds a plain text button.
    ReplyKeyboardBuilder& text(std::string label) {
        KeyboardButton b;
        b.text = std::move(label);
        return button(std::move(b));
    }

    /// Adds a button that asks the user to share their phone number.
    ReplyKeyboardBuilder& requestContact(std::string label) {
        KeyboardButton b;
        b.text = std::move(label);
        b.request_contact = true;
        return button(std::move(b));
    }

    /// Adds a button that asks the user to share their location.
    ReplyKeyboardBuilder& requestLocation(std::string label) {
        KeyboardButton b;
        b.text = std::move(label);
        b.request_location = true;
        return button(std::move(b));
    }

    /// Adds a fully custom button.
    ReplyKeyboardBuilder& button(KeyboardButton b) {
        if (rows_.empty()) {
            rows_.emplace_back();
        }
        rows_.back().push_back(std::move(b));
        return *this;
    }

    /// Starts a new row; empty rows are dropped by build().
    ReplyKeyboardBuilder& row() {
        rows_.emplace_back();
        return *this;
    }

    /// Requests clients to shrink the keyboard vertically.
    ReplyKeyboardBuilder& resize(bool value = true) {
        resize_ = value;
        return *this;
    }

    /// Hides the keyboard after one use.
    ReplyKeyboardBuilder& oneTime(bool value = true) {
        one_time_ = value;
        return *this;
    }

    /// Shows the keyboard only to targeted users.
    ReplyKeyboardBuilder& selective(bool value = true) {
        selective_ = value;
        return *this;
    }

    /// Placeholder shown in the input field while the keyboard is active.
    ReplyKeyboardBuilder& placeholder(std::string text) {
        placeholder_ = std::move(text);
        return *this;
    }

    /// Keeps the keyboard always shown.
    ReplyKeyboardBuilder& persistent(bool value = true) {
        persistent_ = value;
        return *this;
    }

    /// Produces the markup (usable directly as a @c reply_markup value).
    ReplyKeyboardMarkup build() const {
        ReplyKeyboardMarkup markup;
        for (const auto& row : rows_) {
            if (!row.empty()) {
                markup.keyboard.push_back(row);
            }
        }
        if (resize_) {
            markup.resize_keyboard = true;
        }
        if (one_time_) {
            markup.one_time_keyboard = true;
        }
        if (selective_) {
            markup.selective = true;
        }
        if (persistent_) {
            markup.is_persistent = true;
        }
        if (!placeholder_.empty()) {
            markup.input_field_placeholder = placeholder_;
        }
        return markup;
    }

private:
    std::vector<std::vector<KeyboardButton>> rows_;
    std::string placeholder_;
    bool resize_ = false;
    bool one_time_ = false;
    bool selective_ = false;
    bool persistent_ = false;
};

}  // namespace tgbot
