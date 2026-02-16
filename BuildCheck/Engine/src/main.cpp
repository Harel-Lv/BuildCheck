#include "utils/httplib.h"
#include <iostream>
#include <cstdlib>
#include <string>

void register_engine_routes(httplib::Server& server);

int main() {
    const char* allow_stub_env = std::getenv("ALLOW_CPP_ENGINE_STUB");
    const std::string allow_stub = (allow_stub_env && *allow_stub_env) ? allow_stub_env : "";
    if (!(allow_stub == "1" || allow_stub == "true" || allow_stub == "TRUE")) {
        std::cerr << "[ENGINE] C++ stub runtime is disabled by default.\n";
        std::cerr << "[ENGINE] Use Python runtime: BuildCheck/Engine/engine_service.py\n";
        std::cerr << "[ENGINE] Set ALLOW_CPP_ENGINE_STUB=1 only for explicit stub testing.\n";
        return 1;
    }

    httplib::Server server;

    // Health
    server.Get("/engine/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.set_content(R"({"ok":true,"service":"engine"})", "application/json");
    });

    register_engine_routes(server);

    const int port = 9090;
    std::cout << "[ENGINE] listening on http://0.0.0.0:" << port << "\n";
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "[ENGINE] failed to listen on port " << port << "\n";
        return 1;
    }
    return 0;
}
