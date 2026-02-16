#pragma once
#include <string>
#include <vector>
#include <stdexcept>

class EngineClientError : public std::runtime_error {
public:
    EngineClientError(std::string message, int status_code = 500, std::string response_body = "")
        : std::runtime_error(std::move(message)),
          status_code_(status_code),
          response_body_(std::move(response_body)) {}

    int status_code() const noexcept { return status_code_; }
    const std::string& response_body() const noexcept { return response_body_; }

private:
    int status_code_;
    std::string response_body_;
};

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

