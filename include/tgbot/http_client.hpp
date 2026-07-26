#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tgbot {

/// @brief One part of a @c multipart/form-data request body.
///
/// A part is either a plain form field (@ref filename empty) or a file upload.
/// File content is given inline as bytes or as a path streamed from disk.
struct MultipartPart {
    /// Form field name (for uploads this is the @c attach:// name).
    std::string name{};
    /// In-memory content (@c std::string holds arbitrary bytes) or a file path.
    std::variant<std::string, std::filesystem::path> data{};
    /// File name presented to the server; empty for plain form fields.
    std::string filename{};
    /// MIME type of the part; empty lets the transport pick a default.
    std::string content_type{};
};

/// @brief A blocking HTTP POST request as issued against the Bot API.
struct HttpRequest {
    /// Absolute URL.
    std::string url{};
    /// Request body; used only when @ref parts is empty.
    std::string body{};
    /// Content type of @ref body.
    std::string content_type = "application/json";
    /// Extra request headers (name, value), e.g. a webhook secret token.
    std::vector<std::pair<std::string, std::string>> headers{};
    /// When non-empty the request is sent as @c multipart/form-data composed
    /// of these parts and @ref body is ignored.
    std::vector<MultipartPart> parts{};
    /// Per-request timeout; zero means "use the client default".
    std::chrono::milliseconds timeout{0};
};

/// @brief Response to an tgbot::HttpRequest.
struct HttpResponse {
    /// HTTP status code (200, 429, ...); never 0 on a successful transport.
    int status_code = 0;
    /// Raw response body.
    std::string body{};
};

/// @brief Abstract blocking HTTP transport used by tgbot::ApiClient.
///
/// The library ships tgbot::CurlHttpClient; tests substitute a mock.
/// Implementations must be safe to call from multiple threads concurrently.
class HttpClient {
public:
    virtual ~HttpClient() = default;

    /// Executes @p request and returns the response.
    /// @throws tgbot::NetworkError on any transport-level failure.
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

}  // namespace tgbot
