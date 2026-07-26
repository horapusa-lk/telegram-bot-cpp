cmake_minimum_required(VERSION 3.20)

project(tgbot
    VERSION 0.1.0
    DESCRIPTION "Complete C++20 client library for the Telegram Bot API"
    HOMEPAGE_URL "https://core.telegram.org/bots/api"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(FetchContent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TGBOT_IS_TOP_LEVEL ON)
else()
    set(TGBOT_IS_TOP_LEVEL OFF)
endif()

option(TGBOT_BUILD_TESTS "Build tgbot unit and integration tests" ${TGBOT_IS_TOP_LEVEL})
option(TGBOT_BUILD_EXAMPLES "Build tgbot example programs" ${TGBOT_IS_TOP_LEVEL})
option(TGBOT_BUILD_DOCS "Add the Doxygen 'docs' target" OFF)
option(TGBOT_SANITIZERS "Build tests with Address/UB sanitizers" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies: prefer an installed package, fall back to FetchContent.
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(CURL QUIET)
if(NOT CURL_FOUND)
    # Configure the vendored curl with plain (directory-scope) variables so a
    # parent project's cache — BUILD_SHARED_LIBS, BUILD_TESTING, its own curl
    # feature choices — is never overwritten. CMP0077 makes option() honor
    # these normal variables inside the curl subproject.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(BUILD_TESTING OFF)
    set(HTTP_ONLY ON)
    set(CURL_ENABLE_EXPORT_TARGET OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_ZLIB OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON)
    elseif(APPLE)
        set(CURL_USE_SECTRANSP ON)
    else()
        set(CURL_USE_OPENSSL ON)
    endif()
    FetchContent_Declare(curl
        URL https://github.com/curl/curl/releases/download/curl-8_11_1/curl-8.11.1.tar.xz
        URL_HASH SHA256=c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56)
    FetchContent_MakeAvailable(curl)
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    endif()
    unset(BUILD_SHARED_LIBS)
    unset(BUILD_TESTING)
endif()

# cpp-httplib powers the embedded webhook server (plain HTTP; TLS terminates
# at the reverse proxy / tunnel in front of the bot).  Plain variables again:
# a parent that consumes httplib itself keeps its own feature configuration.
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF)
FetchContent_MakeAvailable(httplib)

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------
add_library(tgbot
    src/api_client.cpp
    src/api_error.cpp
    src/bot.cpp
    src/curl_http_client.cpp
    src/dispatcher.cpp
    src/input_file.cpp
    src/long_polling.cpp
    src/webhook.cpp
    src/generated/api_methods_0.cpp
    src/generated/api_methods_1.cpp
    src/generated/api_methods_2.cpp
    src/generated/api_methods_3.cpp
    src/generated/collect_files.cpp
    src/generated/method_registry.cpp
    src/generated/registry.cpp
    src/generated/types_json_0.cpp
    src/generated/types_json_1.cpp
    src/generated/types_json_2.cpp
    src/generated/types_json_3.cpp
    src/generated/types_json_4.cpp
    src/generated/types_json_5.cpp)
add_library(tgbot::tgbot ALIAS tgbot)

target_include_directories(tgbot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# httplib is header-only and used only inside src/, so it is a private include
# rather than a linked target — this keeps the installed export free of any
# reference to it.
#
# The BUILD/INSTALL interface split lets the build link the FetchContent
# targets while the installed export refers to the namespaced imported names,
# which tgbotConfig.cmake recreates via find_dependency().
# curl and Threads are PUBLIC because tgbot is a static library: consumers
# must link them at their final link step regardless of them being
# implementation details.
find_package(Threads REQUIRED)
target_link_libraries(tgbot
    PUBLIC nlohmann_json::nlohmann_json CURL::libcurl Threads::Threads)
target_include_directories(tgbot SYSTEM PRIVATE ${httplib_SOURCE_DIR})
target_compile_features(tgbot PUBLIC cxx_std_20)

if(MSVC)
    set(TGBOT_WARNINGS /W4 /WX)
    target_compile_options(tgbot PRIVATE ${TGBOT_WARNINGS} /utf-8)
    target_compile_definitions(tgbot PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    set(TGBOT_WARNINGS -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(tgbot PRIVATE ${TGBOT_WARNINGS})
endif()

if(TGBOT_SANITIZERS AND NOT MSVC)
    target_compile_options(tgbot PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(tgbot PUBLIC -fsanitize=address,undefined)
endif()

set_target_properties(tgbot PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# ---------------------------------------------------------------------------
# Install / export
#
# Export rules are only valid when the dependencies are installed packages
# (imported targets may be referenced from an export; FetchContent-built
# in-tree targets may not). The FetchContent path is for embedding tgbot into
# another build, where installation is not used.
# ---------------------------------------------------------------------------
if(nlohmann_json_FOUND AND CURL_FOUND)
    set(TGBOT_INSTALL_DEFAULT ON)
else()
    set(TGBOT_INSTALL_DEFAULT OFF)
endif()
option(TGBOT_INSTALL
    "Generate install/export rules (requires nlohmann_json and CURL via find_package)"
    ${TGBOT_INSTALL_DEFAULT})

if(TGBOT_INSTALL)
install(TARGETS tgbot EXPORT tgbotTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/tgbot DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT tgbotTargets
    NAMESPACE tgbot::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tgbot)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/tgbotConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tgbotConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tgbot)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/tgbotConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tgbotConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tgbotConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tgbot)
endif()  # TGBOT_INSTALL

# ---------------------------------------------------------------------------
# Optional components
# ---------------------------------------------------------------------------
if(TGBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(TGBOT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(TGBOT_BUILD_DOCS)
    add_subdirectory(docs)
endif()
