#pragma once

// Shared helper for the examples: read configuration from the environment
// (and optionally a .env file in the working directory) so no example ever
// hard-codes a token.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace examples {

/// Loads KEY=VALUE lines from .env into the process environment (existing
/// variables win).  Lines starting with '#' are comments.
inline void load_dotenv(const char* path = ".env") {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
#ifdef _WIN32
        if (std::getenv(key.c_str()) == nullptr) {
            _putenv_s(key.c_str(), value.c_str());
        }
#else
        setenv(key.c_str(), value.c_str(), /*overwrite=*/0);
#endif
    }
}

/// Returns the environment variable or exits with a helpful message.
inline std::string require_env(const char* name) {
    load_dotenv();
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        std::fprintf(stderr,
                     "error: environment variable %s is not set.\n"
                     "Copy .env.example to .env and fill it in, or export it.\n",
                     name);
        std::exit(1);
    }
    return value;
}

}  // namespace examples
