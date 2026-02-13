#pragma once
#include <string>
#include <vector>

class EngineClient {
public:
    EngineClient(std::string host = "127.0.0.1", int port = 9090, std::string api_key = "")
        : host_(std::move(host)), port_(port), api_key_(std::move(api_key)) {}

    // מחזיר JSON של ה-Engine
    std::string analyze_paths_json(const std::string& request_id,
                                   const std::vector<std::string>& image_paths,
                                   const std::string& rate_limit_key = "") const;

private:
    std::string host_;
    int port_;
    std::string api_key_;
};

