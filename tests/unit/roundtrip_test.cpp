#ifdef _MSC_VER
#pragma warning(disable : 4996)  // std::getenv in test-only trace code
#endif

#include <cstdio>
#include <cstdlib>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "generated/roundtrip.hpp"

using tgbot::test::RoundtripCase;

namespace {

void run_corpus(const std::vector<RoundtripCase>& cases) {
    for (const RoundtripCase& c : cases) {
        if (std::getenv("TGBOT_TRACE_ROUNDTRIP") != nullptr) {
            std::fprintf(stderr, "[roundtrip] %s\n", c.name);
            std::fflush(stderr);
        }
        INFO("type: " << c.name);
        const nlohmann::json sample = nlohmann::json::parse(c.sample);
        nlohmann::json reserialized;
        REQUIRE_NOTHROW(reserialized = c.roundtrip(sample));
        INFO("sample:       " << sample.dump());
        INFO("reserialized: " << reserialized.dump());
        CHECK(reserialized == sample);
    }
}

}  // namespace

TEST_CASE("generated types round-trip through JSON, shard 0", "[roundtrip]") {
    run_corpus(tgbot::test::corpus_0());
}
TEST_CASE("generated types round-trip through JSON, shard 1", "[roundtrip]") {
    run_corpus(tgbot::test::corpus_1());
}
TEST_CASE("generated types round-trip through JSON, shard 2", "[roundtrip]") {
    run_corpus(tgbot::test::corpus_2());
}
TEST_CASE("generated types round-trip through JSON, shard 3", "[roundtrip]") {
    run_corpus(tgbot::test::corpus_3());
}
