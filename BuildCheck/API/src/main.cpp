#include <iostream>
#include <cstdlib>
#include <string>
#include "utils/httplib.h"
#include "routes/register_routes.h"
#include "services/engine_client.h"

int main() {
    httplib::Server server;
    const char* env_host = std::getenv("ENGINE_HOST");
    const char* env_port = std::getenv("ENGINE_PORT");
    const char* env_key = std::getenv("ENGINE_API_KEY");

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
    EngineClient engine(engine_host, engine_port, engine_api_key);

    register_routes(server, engine);

    std::cout << "BuildCheck API running on http://127.0.0.1:8080\n";
    server.listen("0.0.0.0", 8080);
}

