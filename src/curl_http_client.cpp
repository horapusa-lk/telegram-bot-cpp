#include "tgbot/curl_http_client.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include <curl/curl.h>

#include "tgbot/api_error.hpp"

namespace tgbot {

namespace {

// curl_global_init must run exactly once before any easy handle exists and is
// not thread-safe itself, hence the function-local static.
void ensure_curl_global() {
    static const int rc = [] { return static_cast<int>(curl_global_init(CURL_GLOBAL_DEFAULT)); }();
    if (rc != 0) {
        throw NetworkError("curl_global_init failed");
    }
}

struct EasyDeleter {
    void operator()(CURL* h) const noexcept { curl_easy_cleanup(h); }
};
using EasyHandle = std::unique_ptr<CURL, EasyDeleter>;

struct MimeDeleter {
    void operator()(curl_mime* m) const noexcept { curl_mime_free(m); }
};
using MimeHandle = std::unique_ptr<curl_mime, MimeDeleter>;

struct SlistDeleter {
    void operator()(curl_slist* l) const noexcept { curl_slist_free_all(l); }
};
using SlistHandle = std::unique_ptr<curl_slist, SlistDeleter>;

extern "C" size_t tgbot_curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

CurlHttpClient::CurlHttpClient() : CurlHttpClient(Options{}) {}

CurlHttpClient::CurlHttpClient(Options options) : options_(std::move(options)) {
    ensure_curl_global();
}

CurlHttpClient::~CurlHttpClient() = default;

HttpResponse CurlHttpClient::send(const HttpRequest& request) {
    // One persistent handle per thread keeps HTTPS connections alive across
    // calls (essential for long polling) without any locking.
    thread_local EasyHandle handle{[] {
        ensure_curl_global();
        return curl_easy_init();
    }()};
    if (!handle) {
        throw NetworkError("curl_easy_init failed");
    }
    CURL* h = handle.get();
    curl_easy_reset(h);

    std::string response_body;
    char error_buffer[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(h, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, options_.user_agent.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, &tgbot_curl_write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(options_.connect_timeout.count()));
    const auto timeout = request.timeout.count() > 0 ? request.timeout : options_.default_timeout;
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));

    SlistHandle headers;
    MimeHandle mime;
    curl_slist* header_list = nullptr;
    for (const auto& [name, value] : request.headers) {
        header_list = curl_slist_append(header_list, (name + ": " + value).c_str());
    }
    if (request.parts.empty()) {
        header_list =
            curl_slist_append(header_list, ("Content-Type: " + request.content_type).c_str());
        headers.reset(header_list);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    } else {
        if (header_list != nullptr) {
            headers.reset(header_list);
            curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers.get());
        }
        mime.reset(curl_mime_init(h));
        for (const auto& part : request.parts) {
            curl_mimepart* p = curl_mime_addpart(mime.get());
            curl_mime_name(p, part.name.c_str());
            if (const auto* path = std::get_if<std::filesystem::path>(&part.data)) {
                curl_mime_filedata(p, path->string().c_str());
            } else {
                const auto& bytes = std::get<std::string>(part.data);
                curl_mime_data(p, bytes.data(), bytes.size());
            }
            if (!part.filename.empty()) {
                curl_mime_filename(p, part.filename.c_str());
            }
            if (!part.content_type.empty()) {
                curl_mime_type(p, part.content_type.c_str());
            }
        }
        curl_easy_setopt(h, CURLOPT_MIMEPOST, mime.get());
    }

    const CURLcode rc = curl_easy_perform(h);
    if (rc != CURLE_OK) {
        // curl error strings mention at most the host, never the URL path, so
        // the bot token cannot leak into exception messages.
        std::string detail = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(rc);
        throw NetworkError("HTTP request failed: " + detail);
    }

    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    return HttpResponse{static_cast<int>(status), std::move(response_body)};
}

}  // namespace tgbot
