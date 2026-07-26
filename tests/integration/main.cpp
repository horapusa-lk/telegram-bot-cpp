// Custom Catch2 main for the integration suite: loads .env from the current
// directory (or TGBOT_ENV_FILE) into the process environment before any test
// reads it.  Credentials never appear in the repository or in test sources.

#ifdef _MSC_VER
#pragma warning(disable : 4996)  // std::getenv
#endif

#include <cstdlib>
#include <fstream>
#include <string>

#include <catch2/catch_session.hpp>

namespace {

void load_dotenv(const char* path) {
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

}  // namespace

int main(int argc, char* argv[]) {
    const char* env_file = std::getenv("TGBOT_ENV_FILE");
    load_dotenv(env_file != nullptr ? env_file : ".env");
    return Catch::Session().run(argc, argv);
}
