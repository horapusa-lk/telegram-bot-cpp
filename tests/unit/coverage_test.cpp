// The coverage gate: every type and method in spec/api_inventory.json must be
// implemented.  The registry entries are compile-time proven (the generated
// registry takes addresses of the real symbols), so this test failing means
// the inventory gained entries the generated code does not cover — re-run
// tools/generate.py.

#include <fstream>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <tgbot/detail/registry.hpp>

namespace {

nlohmann::json load_inventory() {
    std::ifstream in(TGBOT_SPEC_PATH);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

}  // namespace

TEST_CASE("every documented type is implemented", "[coverage]") {
    const nlohmann::json inventory = load_inventory();
    std::set<std::string> implemented;
    for (const auto name : tgbot::detail::implemented_types()) {
        implemented.emplace(name);
    }
    for (const auto& type : inventory.at("types")) {
        const auto name = type.at("name").get<std::string>();
        INFO("missing type: " << name);
        CHECK(implemented.count(name) == 1);
    }
    CHECK(inventory.at("types").size() <= implemented.size());
}

TEST_CASE("every documented method is implemented", "[coverage]") {
    const nlohmann::json inventory = load_inventory();
    std::set<std::string> implemented;
    for (const auto name : tgbot::detail::implemented_methods()) {
        implemented.emplace(name);
    }
    for (const auto& method : inventory.at("methods")) {
        const auto name = method.at("name").get<std::string>();
        INFO("missing method: " << name);
        CHECK(implemented.count(name) == 1);
    }
    CHECK(inventory.at("methods").size() == implemented.size());
}
