#include "services/engine_client.h"
#include "utils/httplib.h"
#include "third_party/json.hpp"

#include <stdexcept>
using nlohmann::json;
std::string EngineClient::analyze_paths_json(const std::string& request_id,
                                             const std::vector<std::string>& image_paths) const {
    httplib::Client cli(host_, port_);
    cli.set_read_timeout(60, 0);

    json payload;
    payload["request_id"] = request_id;
    payload["paths"] = image_paths;

    auto r = cli.Post("/engine/analyze", payload.dump(), "application/json");
    if (!r) throw std::runtime_error("ENGINE_UNREACHABLE");
    if (r->status != 200) {
    throw std::runtime_error(
        "ENGINE_BAD_STATUS status=" + std::to_string(r->status) +
        " body=" + r->body
    );
}

    return r->body;
}


