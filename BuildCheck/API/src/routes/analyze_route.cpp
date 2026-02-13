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
#include <filesystem>
#include <cstdlib>

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

// ----------------- validation helpers -----------------

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
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

static std::string sanitize_filename(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (name.empty()) return "image.bin";
    return name;
}

// ----------------- route -----------------

void register_analyze_route(httplib::Server& server, const EngineClient& engine) {
    server.Options("/api/property/analyze", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    server.Post("/api/property/analyze", [&engine](const httplib::Request& req, httplib::Response& res) {
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
        final_res.request_id = request_id;
        final_res.ok = false;

        const std::size_t max_bytes = 10 * 1024 * 1024;

        struct ValidMap { std::string filename; std::size_t idx; };
        std::vector<ValidMap> valid_map;
        valid_map.reserve(files.size());

        // [CHANGE #2] collect temp file paths for engine (instead of bytes)
        std::vector<std::string> temp_paths;
        temp_paths.reserve(files.size());

        std::filesystem::path temp_dir;
        try {
            const char* shared_tmp = std::getenv("BUILDCHECK_SHARED_TMP");
            if (shared_tmp && *shared_tmp) {
                temp_dir = std::filesystem::path(shared_tmp);
            } else {
                temp_dir = std::filesystem::temp_directory_path() / "buildcheck_api";
            }
        } catch (const std::exception& e) {
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR",
                                      std::string("Failed to resolve temp directory: ") + e.what()));
            finish_log(res.status);
            return;
        }

        std::error_code mk_ec;
        std::filesystem::create_directories(temp_dir, mk_ec);
        if (mk_ec) {
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", "Failed to create temp directory"));
            finish_log(res.status);
            return;
        }

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

            std::string safe_name = sanitize_filename(f.filename);
            std::filesystem::path tmp_path =
                temp_dir / (request_id + "_" + std::to_string(temp_paths.size()) + "_" + safe_name);

            std::ofstream out(tmp_path, std::ios::binary);
            if (!out.is_open()) {
                r.ok = false; r.error = "Failed to open temp file";
                final_res.results.push_back(r);
                continue;
            }
            out.write(f.content.data(), (std::streamsize)f.content.size());
            if (!out.good()) {
                out.close();
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
                r.ok = false; r.error = "Failed to write temp file";
                final_res.results.push_back(r);
                continue;
            }
            out.close();

            temp_paths.push_back(tmp_path.string());

            r.ok = false; // will be set from engine result only
            r.error = "Pending engine analysis";
            final_res.results.push_back(r);

            // NOTE: order-based mapping (engine returns in same order)
            valid_map.push_back({f.filename, final_res.results.size() - 1});
        }

        // [CHANGE #3] if no valid images -> return as-is
        if (temp_paths.empty()) {
            final_res.ok = false;
            std::string body = final_res.to_json();
            send_json(res, 200, request_id, body);
            finish_log(res.status);
            return;
        }

        // [CHANGE #4] call engine via HTTP (paths json) and merge by order
        try {
            auto cleanup_temp_files = [&temp_paths]() {
                for (const auto& p : temp_paths) {
                    std::error_code rm_ec;
                    std::filesystem::remove(p, rm_ec);
                }
            };

            const std::string engine_json = engine.analyze_paths_json(request_id, temp_paths);

            // cleanup temp files
            cleanup_temp_files();

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

                // Mark any not-mapped images as failed instead of keeping placeholder state.
                for (size_t i = n; i < valid_map.size(); ++i) {
                    const size_t out_idx = valid_map[i].idx;
                    final_res.results[out_idx].ok = false;
                    final_res.results[out_idx].error = "Missing engine result for image";
                }
            }

            // final ok if any image ok
            final_res.ok = false;
            for (const auto& r : final_res.results) {
                if (r.ok) { final_res.ok = true; break; }
            }

            std::string body = final_res.to_json();
            send_json(res, 200, request_id, body);
            finish_log(res.status);
            return;
        }
        catch (const std::exception& e) {
            for (const auto& p : temp_paths) {
                std::error_code rm_ec;
                std::filesystem::remove(p, rm_ec);
            }
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", e.what()));
            finish_log(res.status);
            return;
        }
        catch (...) {
            for (const auto& p : temp_paths) {
                std::error_code rm_ec;
                std::filesystem::remove(p, rm_ec);
            }
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", "Unknown error"));
            finish_log(res.status);
            return;
        }
    });
}
