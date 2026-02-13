#include "routes/register_routes.h"
#include "routes/analyze_route.h"

void register_routes(httplib::Server& server, const EngineClient& engine) {

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(R"({"status":"ok","service":"BuildCheck API"})", "application/json");
    });

    register_analyze_route(server, engine);
}
