#include "tgbot/dispatcher.hpp"

#include <utility>

namespace tgbot {

std::shared_ptr<const Dispatcher::Handlers> Dispatcher::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return handlers_;
}

void Dispatcher::onUpdate(UpdateHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto next = std::make_shared<Handlers>(*handlers_);
    next->update.push_back(std::move(handler));
    handlers_ = std::move(next);
}

void Dispatcher::onError(ErrorHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto next = std::make_shared<Handlers>(*handlers_);
    next->error = std::move(handler);
    handlers_ = std::move(next);
}

void Dispatcher::dispatch(const Update& update) const {
    const auto handlers = snapshot();
    for (const UpdateHandler& handler : handlers->update) {
        try {
            handler(update);
        } catch (...) {
            if (handlers->error) {
                handlers->error(std::current_exception());
            }
        }
    }
}

void Dispatcher::dispatch_json(const nlohmann::json& payload) const {
    dispatch(payload.get<Update>());
}

void Dispatcher::dispatch_error(std::exception_ptr error) const {
    const auto handlers = snapshot();
    if (handlers->error) {
        handlers->error(std::move(error));
    }
}

}  // namespace tgbot
