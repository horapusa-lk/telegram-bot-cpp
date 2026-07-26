#pragma once

#include <vector>

#include <tgbot/api.hpp>

namespace tgbot::test {

/// One generated smoke case: invoke the method with default params against a
/// mock transport primed with a minimal typed result.
struct MethodSmokeCase {
    const char* name;
    const char* result_json;
    void (*invoke)(const tgbot::Api& api);
};

const std::vector<MethodSmokeCase>& method_corpus();

}  // namespace tgbot::test
