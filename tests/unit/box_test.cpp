#if defined(__GNUC__) && !defined(__clang__)
// GCC at -O3 issues a false-positive maybe-uninitialized inside unique_ptr
// when deep-copying an engaged optional<Box<Node>> below.
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <tgbot/box.hpp>

using tgbot::Box;

namespace {

struct Node {
    std::string label;
    std::optional<Box<Node>> child;  // the recursive pattern the generator emits
};

}  // namespace

TEST_CASE("Box value-initializes by default", "[box]") {
    Box<int> b;
    CHECK(*b == 0);
}

TEST_CASE("Box copies deeply", "[box]") {
    Box<std::string> a{std::string("hello")};
    Box<std::string> b = a;
    *b += " world";
    CHECK(*a == "hello");
    CHECK(*b == "hello world");
}

TEST_CASE("Box copy assignment replaces the value", "[box]") {
    Box<int> a{1};
    Box<int> b{2};
    b = a;
    *a = 99;
    CHECK(*b == 1);
}

TEST_CASE("Box move steals the allocation", "[box]") {
    Box<std::string> a{std::string("payload")};
    const std::string* before = &*a;
    Box<std::string> b = std::move(a);
    CHECK(&*b == before);
}

TEST_CASE("Box compares by value", "[box]") {
    CHECK(Box<int>{5} == Box<int>{5});
    CHECK_FALSE(Box<int>{5} == Box<int>{6});
}

TEST_CASE("recursive structures via optional<Box<T>> deep-copy", "[box]") {
    Node root{"root", Box<Node>{Node{"leaf", std::nullopt}}};
    Node copy = root;
    (*copy.child)->label = "changed";
    CHECK((*root.child)->label == "leaf");
    CHECK((*copy.child)->label == "changed");
}

TEST_CASE("Box works through pointer-like access", "[box]") {
    Box<Node> b{Node{"x", std::nullopt}};
    CHECK(b->label == "x");
    b->label = "y";
    CHECK((*b).label == "y");
}
