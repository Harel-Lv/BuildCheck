#pragma once
#include <string>
#include <vector>

class EngineClient {
public:
    EngineClient(std::string host = "127.0.0.1", int port = 9090)
        : host_(std::move(host)), port_(port) {}

    // מחזיר JSON של ה-Engine
    std::string analyze_paths_json(const std::string& request_id,
                                   const std::vector<std::string>& image_paths) const;

private:
    std::string host_;
    int port_;
};

