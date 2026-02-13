#include "utils/httplib.h"
#include "../../third_party/json.hpp"

using nlohmann::json;

void register_analyze_route(httplib::Server& server) {
    server.Post("/engine/analyze", [](const httplib::Request&, httplib::Response& res) {
        // This C++ route is intentionally disabled.
        // Production analyze runtime is BuildCheck/Engine/engine_service.py.
        res.status = 501;
        res.set_header("Content-Type", "application/json");
        res.set_content(
            json{
                {"ok", false},
                {"error", "cpp_engine_stub_disabled"},
                {"message", "Use Python engine_service.py runtime for /engine/analyze"}
            }.dump(),
            "application/json"
        );
    });
}
