#include "tgbot/webhook.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <httplib.h>

#include "tgbot/api_error.hpp"

namespace tgbot {

nlohmann::json answer_webhook_with(const std::string& method, nlohmann::json params) {
    params["method"] = method;
    return params;
}

namespace {

// httplib interprets patterns as ECMAScript regexes; the documented contract
// of WebhookOptions::path is a literal path, so escape the metacharacters.
std::string escape_regex(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (std::string_view(R"(.^$|()[]{}*+?\)").find(c) != std::string_view::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

struct WebhookServer::Impl {
    const Dispatcher& dispatcher;
    WebhookOptions options;
    httplib::Server server;
    std::thread server_thread;
    int bound_port = 0;

    // Worker pool: raw payloads queued by the HTTP handler, parsed and
    // dispatched off the request thread.  The queue is bounded; when it is
    // full the server answers 503 so Telegram redelivers later.
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::string> queue;
    std::vector<std::thread> workers;
    bool draining = false;  // guarded by queue_mutex
    std::atomic<bool> stopped{false};
    std::mutex join_mutex;  // serializes joining server_thread

    Impl(const Dispatcher& d, WebhookOptions o) : dispatcher(d), options(std::move(o)) {
        server.set_payload_max_length(options.max_request_body);
        server.Post(
            escape_regex(options.path),
            [this](const httplib::Request& req, httplib::Response& res) { handle(req, res); });
    }

    void handle(const httplib::Request& req, httplib::Response& res) {
        if (!options.secret_token.empty() &&
            req.get_header_value("X-Telegram-Bot-Api-Secret-Token") != options.secret_token) {
            res.status = 403;
            res.set_content("forbidden", "text/plain");
            return;
        }
        nlohmann::json payload = nlohmann::json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            res.status = 400;
            res.set_content("bad request", "text/plain");
            return;
        }

        if (options.sync_responder) {
            try {
                if (auto reply = options.sync_responder(payload.get<Update>())) {
                    if (!enqueue(req.body)) {
                        res.status = 503;
                        res.set_content("queue full", "text/plain");
                        return;
                    }
                    res.status = 200;
                    res.set_content(reply->dump(), "application/json");
                    return;
                }
            } catch (...) {
                report(std::current_exception());
            }
        }
        if (!enqueue(req.body)) {
            res.status = 503;  // Telegram retries the delivery later
            res.set_content("queue full", "text/plain");
            return;
        }
        res.status = 200;
        res.set_content("", "text/plain");
    }

    /// Returns false when the bounded queue is full.
    bool enqueue(std::string body) {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            if (queue.size() >= options.max_queue_size) {
                return false;
            }
            queue.push_back(std::move(body));
        }
        queue_cv.notify_one();
        return true;
    }

    void report(std::exception_ptr error) const { dispatcher.dispatch_error(std::move(error)); }

    void worker_loop() {
        for (;;) {
            std::string body;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return draining || !queue.empty(); });
                if (queue.empty()) {
                    return;  // draining and nothing left
                }
                body = std::move(queue.front());
                queue.pop_front();
            }
            try {
                dispatcher.dispatch_json(nlohmann::json::parse(body));
            } catch (...) {
                dispatcher.dispatch_error(std::current_exception());
            }
        }
    }

    void start_workers() {
        {
            const std::lock_guard<std::mutex> lock(queue_mutex);
            draining = false;
        }
        workers.reserve(options.worker_threads);
        for (std::size_t i = 0; i < options.worker_threads; ++i) {
            workers.emplace_back([this] { worker_loop(); });
        }
    }

    void join_server_thread() {
        // Both run() and shutdown() funnel through here; the mutex makes the
        // joinable()/join() pair atomic so exactly one thread joins.
        const std::lock_guard<std::mutex> lock(join_mutex);
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    void shutdown() {
        // A worker cannot join itself: stop() from inside an update handler
        // would deadlock, so reject it loudly instead.
        const auto self = std::this_thread::get_id();
        if (std::any_of(workers.begin(), workers.end(),
                        [self](const std::thread& t) { return t.get_id() == self; })) {
            throw Error(
                "WebhookServer::stop() must not be called from an update handler; "
                "call it from another thread");
        }
        if (stopped.exchange(true)) {
            return;  // idempotent: a second stop() (e.g. the destructor) is a no-op
        }
        server.stop();
        join_server_thread();
        {
            // The store must happen under the mutex: otherwise a worker that
            // just evaluated its predicate could block forever on a lost
            // notification and this join would deadlock.
            const std::lock_guard<std::mutex> lock(queue_mutex);
            draining = true;
        }
        queue_cv.notify_all();
        for (std::thread& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers.clear();
    }
};

WebhookServer::WebhookServer(const Dispatcher& dispatcher, WebhookOptions options)
    : impl_(std::make_unique<Impl>(dispatcher, std::move(options))) {}

WebhookServer::~WebhookServer() {
    try {
        stop();
    } catch (...) {
        // A worker-thread destructor misuse must not terminate the process.
    }
}

bool WebhookServer::start() {
    if (impl_->options.port == 0) {
        impl_->bound_port = impl_->server.bind_to_any_port(impl_->options.host);
        if (impl_->bound_port <= 0) {
            return false;
        }
    } else {
        if (!impl_->server.bind_to_port(impl_->options.host, impl_->options.port)) {
            return false;
        }
        impl_->bound_port = impl_->options.port;
    }
    impl_->start_workers();
    impl_->server_thread = std::thread([this] { impl_->server.listen_after_bind(); });
    impl_->server.wait_until_ready();
    return true;
}

void WebhookServer::run() {
    if (!start()) {
        throw Error("webhook server: cannot bind " + impl_->options.host + ":" +
                    std::to_string(impl_->options.port));
    }
    impl_->join_server_thread();
}

void WebhookServer::stop() {
    impl_->shutdown();
}

int WebhookServer::port() const noexcept {
    return impl_->bound_port;
}

}  // namespace tgbot
