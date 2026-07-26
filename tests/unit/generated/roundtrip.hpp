#pragma once

#include <vector>

#include <nlohmann/json.hpp>
#include <tgbot/types.hpp>

namespace tgbot::test {

/// One synthetic sample: parse `sample` into the C++ type, serialize it back,
/// and compare JSON values for equality.
struct RoundtripCase {
    const char* name;
    const char* sample;
    nlohmann::json (*roundtrip)(const nlohmann::json&);
};

const std::vector<RoundtripCase>& corpus_0();
const std::vector<RoundtripCase>& corpus_1();
const std::vector<RoundtripCase>& corpus_2();
const std::vector<RoundtripCase>& corpus_3();

}  // namespace tgbot::test
