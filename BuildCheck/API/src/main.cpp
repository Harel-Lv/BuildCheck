#include <iostream>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <cctype>
#include "utils/httplib.h"
#include "routes/register_routes.h"
#include "services/engine_client.h"

namespace {
std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int env_int(const char* name, int fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

bool is_engine_key_strong(const std::string& key) {
    const int min_len = std::max(8, env_int("ENGINE_MIN_KEY_LEN", 24));
    if (static_cast<int>(key.size()) < min_len) return false;
    const std::string lowered = to_lower(trim_copy(key));
    return lowered != "change-me" &&
           lowered != "changeme" &&
           lowered != "default" &&
           lowered != "password" &&
           lowered != "123456";
}
} // namespace

int main() {
    httplib::Server server;
    const char* env_host = std::getenv("ENGINE_HOST");
    const char* env_port = std::getenv("ENGINE_PORT");
    const char* env_key = std::getenv("ENGINE_API_KEY");
    const char* env_api_port = std::getenv("API_PORT");

    std::string engine_host = (env_host && *env_host) ? env_host : "127.0.0.1";
    int engine_port = 9090;
    if (env_port && *env_port) {
        try {
            engine_port = std::stoi(env_port);
        } catch (...) {
            engine_port = 9090;
        }
    }

    std::string engine_api_key = (env_key && *env_key) ? env_key : "";
    if (engine_api_key.empty()) {
        std::cerr << "Missing required ENGINE_API_KEY environment variable.\n";
        return 1;
    }
    if (!is_engine_key_strong(engine_api_key)) {
        std::cerr << "ENGINE_API_KEY is weak. Set a stronger key (min len via ENGINE_MIN_KEY_LEN, default 24).\n";
        return 1;
    }
    int api_port = 8080;
    if (env_api_port && *env_api_port) {
        try {
            api_port = std::stoi(env_api_port);
        } catch (...) {
            api_port = 8080;
        }
    }

    EngineClient engine(engine_host, engine_port, engine_api_key);
    std::size_t payload_max = static_cast<std::size_t>(env_int("BUILDCHECK_PAYLOAD_MAX_BYTES", 256 * 1024 * 1024));
    if (payload_max < 1024 * 1024) payload_max = 1024 * 1024;
    server.set_payload_max_length(payload_max);

    register_routes(server, engine);

    std::cout << "BuildCheck API running on http://127.0.0.1:" << api_port << "\n";
    if (!server.listen("0.0.0.0", api_port)) {
        std::cerr << "Failed to listen on port " << api_port << ".\n";
        return 1;
    }
    return 0;
}

