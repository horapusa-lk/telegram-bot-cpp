#include "tgbot/api_client.hpp"

#include <thread>
#include <utility>

#include "tgbot/curl_http_client.hpp"

namespace tgbot {

namespace {

std::shared_ptr<HttpClient> default_http_client() {
    // All ApiClient instances without an explicit transport share one curl
    // client (it is thread-safe and keeps per-thread connections alive).
    static const std::shared_ptr<HttpClient> shared = std::make_shared<CurlHttpClient>();
    return shared;
}

// Values inside multipart bodies are plain strings: JSON strings go verbatim,
// everything else (numbers, bools, objects, arrays) as its JSON text.
std::string form_value(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

}  // namespace

ApiClient::ApiClient(std::string token) : ApiClient(std::move(token), Options()) {}

ApiClient::ApiClient(std::string token, Options options)
    : token_(std::move(token)), options_(std::move(options)) {
    if (!options_.http) {
        options_.http = default_http_client();
    }
}

HttpResponse ApiClient::send_once(const std::string& method, const nlohmann::json& params,
                                  const std::vector<MultipartPart>& files,
                                  std::chrono::milliseconds timeout) const {
    HttpRequest request;
    request.url = options_.base_url + "/bot" + token_ + "/" + method;
    request.timeout = timeout;
    if (files.empty()) {
        request.body = params.dump();
    } else {
        request.parts.reserve(params.size() + files.size());
        for (const auto& [key, value] : params.items()) {
            request.parts.push_back(MultipartPart{key, form_value(value), {}, {}});
        }
        for (const auto& file : files) {
            request.parts.push_back(file);
        }
    }
    return options_.http->send(request);
}

nlohmann::json ApiClient::call(const std::string& method, nlohmann::json params,
                               std::vector<MultipartPart> files,
                               std::chrono::milliseconds timeout_override) const {
    const RetryPolicy& retry = options_.retry;
    const auto timeout = timeout_override.count() > 0 ? timeout_override : options_.request_timeout;

    auto backoff = retry.initial_backoff;
    const int attempts = retry.max_attempts > 0 ? retry.max_attempts : 1;
    for (int attempt = 1;; ++attempt) {
        const bool last = attempt >= attempts;

        // -- transport ------------------------------------------------------
        HttpResponse response;
        try {
            response = send_once(method, params, files, timeout);
        } catch (const NetworkError&) {
            if (last || !retry.retry_on_network_error) {
                throw;
            }
            std::this_thread::sleep_for(backoff);
            backoff = std::chrono::milliseconds(static_cast<long long>(
                static_cast<double>(backoff.count()) * retry.backoff_multiplier));
            continue;
        }

        // -- envelope -------------------------------------------------------
        nlohmann::json envelope = nlohmann::json::parse(response.body, nullptr, false);
        if (envelope.is_discarded() || !envelope.is_object() || !envelope.contains("ok")) {
            // Not a Bot API envelope: a proxy/gateway error page. Retryable.
            if (!last && retry.retry_on_server_error) {
                std::this_thread::sleep_for(backoff);
                backoff = std::chrono::milliseconds(static_cast<long long>(
                    static_cast<double>(backoff.count()) * retry.backoff_multiplier));
                continue;
            }
            throw NetworkError("invalid Bot API response (HTTP " +
                               std::to_string(response.status_code) + ")");
        }

        if (envelope["ok"].get<bool>()) {
            return envelope.contains("result") ? std::move(envelope["result"])
                                               : nlohmann::json(nullptr);
        }

        // -- API error ------------------------------------------------------
        const int code = envelope.value("error_code", response.status_code);
        std::string description = envelope.value("description", "unknown error");
        std::optional<int> retry_after;
        std::optional<std::int64_t> migrate_to_chat_id;
        if (const auto it = envelope.find("parameters"); it != envelope.end() && it->is_object()) {
            if (it->contains("retry_after")) {
                retry_after = (*it)["retry_after"].get<int>();
            }
            if (it->contains("migrate_to_chat_id")) {
                migrate_to_chat_id = (*it)["migrate_to_chat_id"].get<std::int64_t>();
            }
        }

        const bool rate_limited = code == 429 && retry_after.has_value();
        if (rate_limited && retry.retry_on_rate_limit && !last &&
            std::chrono::seconds(*retry_after) <= retry.max_retry_after) {
            std::this_thread::sleep_for(std::chrono::seconds(*retry_after));
            continue;
        }
        const bool server_error = code >= 500 && code < 600;
        if (server_error && retry.retry_on_server_error && !last) {
            std::this_thread::sleep_for(backoff);
            backoff = std::chrono::milliseconds(static_cast<long long>(
                static_cast<double>(backoff.count()) * retry.backoff_multiplier));
            continue;
        }
        throw ApiError(code, std::move(description), retry_after, migrate_to_chat_id);
    }
}

}  // namespace tgbot
