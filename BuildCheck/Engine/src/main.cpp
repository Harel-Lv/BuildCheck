#include "utils/httplib.h"
#include <iostream>

void register_engine_routes(httplib::Server& server);

int main() {
    httplib::Server server;

    // Health
    server.Get("/engine/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.set_content(R"({"ok":true,"service":"engine"})", "application/json");
    });

    register_engine_routes(server);

    const int port = 9090;
    std::cout << "[ENGINE] listening on http://0.0.0.0:" << port << "\n";
    server.listen("0.0.0.0", port);
    return 0;
}
