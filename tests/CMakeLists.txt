find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)

# ---------------------------------------------------------------------------
# Unit tests: no network, transport is mocked.
# ---------------------------------------------------------------------------
add_executable(tgbot_unit_tests
    unit/api_client_test.cpp
    unit/api_request_test.cpp
    unit/bot_test.cpp
    unit/box_test.cpp
    unit/coverage_test.cpp
    unit/dispatcher_test.cpp
    unit/error_test.cpp
    unit/generated/method_smoke_corpus.cpp
    unit/generated/roundtrip_corpus_0.cpp
    unit/generated/roundtrip_corpus_1.cpp
    unit/generated/roundtrip_corpus_2.cpp
    unit/generated/roundtrip_corpus_3.cpp
    unit/input_file_test.cpp
    unit/long_polling_test.cpp
    unit/method_smoke_test.cpp
    unit/roundtrip_test.cpp
    unit/webhook_test.cpp)
target_link_libraries(tgbot_unit_tests PRIVATE tgbot::tgbot Catch2::Catch2WithMain)
target_include_directories(tgbot_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_compile_definitions(tgbot_unit_tests PRIVATE
    TGBOT_SPEC_PATH="${CMAKE_SOURCE_DIR}/spec/api_inventory.json")
target_compile_options(tgbot_unit_tests PRIVATE ${TGBOT_WARNINGS})
if(MSVC)
    target_compile_options(tgbot_unit_tests PRIVATE /utf-8)
endif()

catch_discover_tests(tgbot_unit_tests PROPERTIES LABELS unit)

# ---------------------------------------------------------------------------
# Live integration tests: OFF by default, opt in with
#   -DTGBOT_ENABLE_INTEGRATION_TESTS=ON  and  ctest -L integration
# Credentials come from the environment / a local .env (see .env.example);
# the single test binary runs its phases in order because an active webhook
# blocks getUpdates.
# ---------------------------------------------------------------------------
option(TGBOT_ENABLE_INTEGRATION_TESTS
    "Build the live integration tests (require TG_* environment)" OFF)
if(TGBOT_ENABLE_INTEGRATION_TESTS)
    add_executable(tgbot_integration_tests
        integration/live_test.cpp
        integration/main.cpp)
    target_link_libraries(tgbot_integration_tests PRIVATE tgbot::tgbot Catch2::Catch2)
    target_compile_options(tgbot_integration_tests PRIVATE ${TGBOT_WARNINGS})
    if(MSVC)
        target_compile_options(tgbot_integration_tests PRIVATE /utf-8)
    endif()
    add_test(NAME tgbot_live_integration
        COMMAND tgbot_integration_tests --order decl
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(tgbot_live_integration PROPERTIES
        LABELS integration
        TIMEOUT 300)
endif()
