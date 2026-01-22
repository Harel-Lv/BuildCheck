// API/src/routes/analyze_route.cpp
#include "routes/analyze_route.h"
#include "services/engine_client.h"
#include "utils/httplib.h"
#include "dto/analyze_request.h"
#include "dto/analyze_response.h"

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <chrono>
#include <random>

// [CHANGE #1] needed for temp file write + cleanup + json parse
#include <fstream>
#include <cstdio>
#include "third_party/json.hpp"

// ----------------- logging + response helpers -----------------

static std::string gen_request_id() {
    static thread_local std::mt19937_64 rng{ std::random_device{}() };
    const uint64_t a = rng();
    const uint64_t b = rng();
    std::ostringstream os;
    os << std::hex << a << b; // מספיק טוב כ-ID קצר
    return os.str();
}

static long long ms_since(const std::chrono::steady_clock::time_point& start) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
}

static void set_common_headers(httplib::Response& res, const std::string& request_id) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Content-Type", "application/json");
    res.set_header("X-Request-Id", request_id);
}

static void send_json(httplib::Response& res, int status, const std::string& request_id, const std::string& body) {
    res.status = status;
    set_common_headers(res, request_id);
    res.set_content(body, "application/json");
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string make_error_json(const std::string& request_id,
                                  const std::string& code,
                                  const std::string& message) {
    std::ostringstream os;
    os << R"({"ok":false,"request_id":")" << json_escape(request_id) << R"(","error":{"code":")"
       << json_escape(code) << R"(","message":")" << json_escape(message) << R"("}})";
    return os.str();
}

// inject request_id into AnalyzeResponse JSON without changing DTO
// expects json like {"ok":...}
static std::string inject_request_id(std::string json, const std::string& request_id) {
    if (json.size() < 2 || json[0] != '{') return json;
    std::ostringstream os;
    os << R"({"request_id":")" << json_escape(request_id) << R"(",)";
    // skip '{' and append rest
    os << json.substr(1);
    return os.str();
}

// ----------------- validation helpers -----------------

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static bool is_all_spaces(const std::string& s) {
    for (char c : s) if (!std::isspace((unsigned char)c)) return false;
    return true;
}

static bool parse_year(const std::string& s, int& out_year) {
    if (s.empty() || is_all_spaces(s)) return false;

    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    if (a >= b) return false;

    int val = 0;
    for (std::size_t i = a; i < b; ++i) {
        if (!std::isdigit((unsigned char)s[i])) return false;
        val = val * 10 + (s[i] - '0');
        if (val > 5000) break;
    }
    out_year = val;
    return true;
}

static bool is_vehicle_type_valid(const std::string& s) {
    if (s.size() < 2 || s.size() > 40) return false;
    if (is_all_spaces(s)) return false;

    for (char c : s) {
        unsigned char uc = (unsigned char)c;
        if (std::isalnum(uc) || std::isspace(uc) || c == '-' || c == '_') continue;
        return false;
    }
    return true;
}

static std::string get_extension(const std::string& filename) {
    const auto pos = filename.find_last_of('.');
    if (pos == std::string::npos) return "";
    return filename.substr(pos);
}

static bool is_allowed_image_ext(const std::string& ext_in) {
    const auto ext = to_lower(ext_in);
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp");
}

static bool looks_like_image_by_magic(const std::string& data) {
    if (data.size() < 12) return false;

    if ((unsigned char)data[0] == 0xFF &&
        (unsigned char)data[1] == 0xD8 &&
        (unsigned char)data[2] == 0xFF) return true;

    const unsigned char png_sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    bool is_png = true;
    for (int i = 0; i < 8; ++i) {
        if ((unsigned char)data[i] != png_sig[i]) { is_png = false; break; }
    }
    if (is_png) return true;

    if (data[0]=='R' && data[1]=='I' && data[2]=='F' && data[3]=='F' &&
        data[8]=='W' && data[9]=='E' && data[10]=='B' && data[11]=='P') return true;

    return false;
}

// ----------------- route -----------------

void register_analyze_route(httplib::Server& server, const EngineClient& engine) {
    server.Options("/api/accident/analyze", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    server.Post("/api/accident/analyze", [&engine](const httplib::Request& req, httplib::Response& res) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::string request_id = gen_request_id();

        std::cout << "[REQ " << request_id << "] START "
                  << req.method << " " << req.path << "\n";

        auto finish_log = [&](int status) {
            std::cout << "[REQ " << request_id << "] END status=" << status
                      << " ms=" << ms_since(t0) << "\n";
        };

        // 415 if not multipart
        if (!req.is_multipart_form_data()) {
            const auto body = make_error_json(request_id, "UNSUPPORTED_MEDIA_TYPE",
                                              "Expected multipart/form-data");
            send_json(res, 415, request_id, body);
            finish_log(res.status);
            return;
        }

        const auto& form = req.form;

        if (!form.has_field("vehicle_type")) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "MISSING_FIELD", "Missing vehicle_type"));
            finish_log(res.status);
            return;
        }
        if (!form.has_field("year")) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "MISSING_FIELD", "Missing year"));
            finish_log(res.status);
            return;
        }

        const std::string vehicle_type = form.get_field("vehicle_type");
        const std::string year_str     = form.get_field("year");

        if (!is_vehicle_type_valid(vehicle_type)) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "INVALID_FIELD", "Invalid vehicle_type"));
            finish_log(res.status);
            return;
        }

        int year = 0;
        if (!parse_year(year_str, year)) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "INVALID_FIELD", "Invalid year (digits only)"));
            finish_log(res.status);
            return;
        }
        if (year < 1980 || year > 2035) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "OUT_OF_RANGE", "Year out of range"));
            finish_log(res.status);
            return;
        }

        if (!form.has_file("images")) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "MISSING_FIELD", "Field 'images' not found"));
            finish_log(res.status);
            return;
        }

        const auto files = form.get_files("images");
        if (files.empty()) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "MISSING_FIELD", "No files under 'images'"));
            finish_log(res.status);
            return;
        }

        AnalyzeResponse final_res;
        final_res.vehicle_type = vehicle_type;
        final_res.year = year;
        final_res.ok = false;

        const std::size_t max_bytes = 10 * 1024 * 1024;

        struct ValidMap { std::string filename; std::size_t idx; };
        std::vector<ValidMap> valid_map;
        valid_map.reserve(files.size());

        // [CHANGE #2] collect temp file paths for engine (instead of bytes)
        std::vector<std::string> temp_paths;
        temp_paths.reserve(files.size());

        for (const auto& f : files) {
            AnalyzeImageResult r;
            r.filename = f.filename;

            const std::string ext = get_extension(f.filename);

            if (f.content.empty()) {
                r.ok = false; r.error = "Empty file";
                final_res.results.push_back(r);
                continue;
            }
            if (f.content.size() > max_bytes) {
                r.ok = false; r.error = "File too large";
                final_res.results.push_back(r);
                continue;
            }
            if (!is_allowed_image_ext(ext)) {
                r.ok = false; r.error = "Bad extension";
                final_res.results.push_back(r);
                continue;
            }
            if (!f.content_type.empty() && f.content_type.rfind("image/", 0) != 0) {
                r.ok = false; r.error = "Bad content-type";
                final_res.results.push_back(r);
                continue;
            }
            if (!looks_like_image_by_magic(f.content)) {
                r.ok = false; r.error = "Not an image (signature check failed)";
                final_res.results.push_back(r);
                continue;
            }

            // write temp file (API/tmp must exist)
            std::string tmp_path =
                "tmp/" + request_id + "_" + std::to_string(temp_paths.size()) + "_" + f.filename;

            std::ofstream out(tmp_path, std::ios::binary);
            out.write(f.content.data(), (std::streamsize)f.content.size());
            out.close();

            temp_paths.push_back(tmp_path);

            r.ok = true; // placeholder; will be replaced by engine result
            final_res.results.push_back(r);

            // NOTE: order-based mapping (engine returns in same order)
            valid_map.push_back({f.filename, final_res.results.size() - 1});
        }

        // [CHANGE #3] if no valid images -> return as-is
        if (temp_paths.empty()) {
            final_res.ok = false;
            std::string body = inject_request_id(final_res.to_json(), request_id);
            send_json(res, 200, request_id, body);
            finish_log(res.status);
            return;
        }

        // [CHANGE #4] call engine via HTTP (paths json) and merge by order
        try {
            const std::string engine_json = engine.analyze_paths_json(request_id, temp_paths);

            // cleanup temp files
            for (auto& p : temp_paths) std::remove(p.c_str());

            nlohmann::json ej = nlohmann::json::parse(engine_json, nullptr, false);
            if (ej.is_discarded()) {
                send_json(res, 500, request_id,
                          make_error_json(request_id, "INTERNAL_ERROR", "Engine returned invalid JSON"));
                finish_log(res.status);
                return;
            }

            if (ej.contains("results") && ej["results"].is_array()) {
                const auto& arr = ej["results"];
                const size_t n = std::min({ arr.size(), temp_paths.size(), valid_map.size() });

                for (size_t i = 0; i < n; ++i) {
                    const auto& er = arr[i];
                    const size_t out_idx = valid_map[i].idx;

                    final_res.results[out_idx].ok = er.value("ok", false);

                    // damage_types
                    final_res.results[out_idx].damage_types.clear();
                    if (er.contains("damage_types") && er["damage_types"].is_array()) {
                        for (const auto& dt : er["damage_types"]) {
                            if (dt.is_string()) final_res.results[out_idx].damage_types.push_back(dt.get<std::string>());
                        }
                    }

                    // TEMP: pricing logic (same as stub for now)
                    if (final_res.results[out_idx].ok) {
                        final_res.results[out_idx].cost_min = 500;
                        final_res.results[out_idx].cost_max = 1500;
                    } else {
                        final_res.results[out_idx].error = "Engine failed to analyze image";
                    }
                }
            }

            // final ok if any image ok
            final_res.ok = false;
            for (const auto& r : final_res.results) {
                if (r.ok) { final_res.ok = true; break; }
            }

            std::string body = inject_request_id(final_res.to_json(), request_id);
            send_json(res, 200, request_id, body);
            finish_log(res.status);
            return;
        }
        catch (const std::exception& e) {
            for (auto& p : temp_paths) std::remove(p.c_str());
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", e.what()));
            finish_log(res.status);
            return;
        }
        catch (...) {
            for (auto& p : temp_paths) std::remove(p.c_str());
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", "Unknown error"));
            finish_log(res.status);
            return;
        }
    });
}
