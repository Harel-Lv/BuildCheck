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
#include <unordered_map>
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
    out.reserve(s.size() + 8);
    const char* hex = "0123456789abcdef";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
                break;
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

static std::string extract_engine_error_message(const EngineClientError& e) {
    if (e.response_body().empty()) return "Engine request failed";
    nlohmann::json ej = nlohmann::json::parse(e.response_body(), nullptr, false);
    if (ej.is_discarded()) return "Engine request failed";
    if (ej.contains("error")) {
        if (ej["error"].is_string()) {
            return ej["error"].get<std::string>();
        }
        if (ej["error"].is_object() &&
            ej["error"].contains("message") &&
            ej["error"]["message"].is_string()) {
            return ej["error"]["message"].get<std::string>();
        }
    }
    return "Engine request failed";
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

static std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::string normalize_rate_limit_key(const std::string& raw, const std::string& fallback) {
    std::string key;
    key.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == ':' || c == '-' || c == '_') {
            key.push_back(c);
        }
    }
    if (key.empty()) key = fallback;
    if (key.size() > 128) key.resize(128);
    return key;
}

static bool trust_proxy_headers() {
    const char* env = std::getenv("BUILDCHECK_TRUST_PROXY_HEADERS");
    if (!env || !*env) return false;
    const std::string v = to_lower(trim_copy(env));
    return (v == "1" || v == "true" || v == "yes" || v == "on");
}

static std::string derive_rate_limit_key(const httplib::Request& req, const std::string& request_id) {
    std::string candidate;
    if (trust_proxy_headers()) {
        const std::string xff = req.get_header_value("X-Forwarded-For");
        if (!xff.empty()) {
            const auto comma = xff.find(',');
            candidate = trim_copy(xff.substr(0, comma));
        }
    }
    if (candidate.empty()) {
        candidate = req.remote_addr;
    }
    return normalize_rate_limit_key(candidate, "req_" + request_id);
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

        std::size_t max_files = 20;
        constexpr std::size_t kMaxFilesHardCap = 100;
        if (const char* env_max_files = std::getenv("BUILDCHECK_MAX_FILES"); env_max_files && *env_max_files) {
            try {
                max_files = std::max<std::size_t>(1, static_cast<std::size_t>(std::stoul(env_max_files)));
                max_files = std::min(max_files, kMaxFilesHardCap);
            } catch (...) {
                max_files = 20;
            }
        }
        if (files.size() > max_files) {
            send_json(res, 400, request_id,
                      make_error_json(request_id, "TOO_MANY_FILES",
                                      "Too many files in one request"));
            finish_log(res.status);
            return;
        }

        AnalyzeResponse final_res;
        final_res.request_id = request_id;
        final_res.ok = false;

        const std::size_t max_bytes = 10 * 1024 * 1024;

        struct ValidMap { std::string temp_path; std::size_t idx; };
        std::vector<ValidMap> valid_map;
        valid_map.reserve(files.size());
        std::unordered_map<std::string, std::size_t> path_to_out_idx;
        path_to_out_idx.reserve(files.size());

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
                                      "Failed to resolve temp directory"));
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
            out.flush();
            if (!out.good()) {
                out.close();
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
                r.ok = false; r.error = "Failed to flush temp file";
                final_res.results.push_back(r);
                continue;
            }
            out.close();
            std::error_code sz_ec;
            const auto saved_size = std::filesystem::file_size(tmp_path, sz_ec);
            if (sz_ec || saved_size != f.content.size()) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
                r.ok = false; r.error = "Temp file size mismatch";
                final_res.results.push_back(r);
                continue;
            }

            temp_paths.push_back(tmp_path.string());

            r.ok = false; // will be set from engine result only
            r.error = "Pending engine analysis";
            final_res.results.push_back(r);

            // NOTE: order-based mapping (engine returns in same order)
            const std::size_t out_idx = final_res.results.size() - 1;
            valid_map.push_back({tmp_path.string(), out_idx});
            path_to_out_idx[tmp_path.string()] = out_idx;
        }

        // [CHANGE #3] if no valid images -> return as-is
        if (temp_paths.empty()) {
            final_res.ok = false;
            std::string body = final_res.to_json();
            send_json(res, 422, request_id, body);
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

            const std::string rl_key = derive_rate_limit_key(req, request_id);
            const std::string engine_json = engine.analyze_paths_json(request_id, temp_paths, rl_key);

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
                std::vector<bool> filled(final_res.results.size(), false);
                std::size_t fallback_i = 0;

                for (const auto& er : arr) {
                    bool has_out_idx = false;
                    std::size_t out_idx = 0;

                    if (er.contains("path") && er["path"].is_string()) {
                        const std::string p = er["path"].get<std::string>();
                        const auto it = path_to_out_idx.find(p);
                        if (it != path_to_out_idx.end()) {
                            out_idx = it->second;
                            has_out_idx = true;
                        }
                    }

                    if (!has_out_idx) {
                        while (fallback_i < valid_map.size() && filled[valid_map[fallback_i].idx]) {
                            ++fallback_i;
                        }
                        if (fallback_i < valid_map.size()) {
                            out_idx = valid_map[fallback_i].idx;
                            has_out_idx = true;
                            ++fallback_i;
                        }
                    }

                    if (!has_out_idx) {
                        continue;
                    }

                    filled[out_idx] = true;

                    final_res.results[out_idx].ok = er.value("ok", false);
                    final_res.results[out_idx].inference_mode = er.value("inference_mode", "");

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
                        final_res.results[out_idx].error.clear();
                    } else {
                        final_res.results[out_idx].error = er.value("error", "Engine failed to analyze image");
                    }
                }

                // Mark any not-mapped images as failed instead of keeping placeholder state.
                for (const auto& vm : valid_map) {
                    if (!filled[vm.idx]) {
                        final_res.results[vm.idx].ok = false;
                        final_res.results[vm.idx].error = "Missing engine result for image";
                    }
                }
            } else {
                std::string engine_err = "Engine returned no results";
                if (ej.contains("error")) {
                    if (ej["error"].is_string()) {
                        engine_err = ej["error"].get<std::string>();
                    } else if (ej["error"].is_object() &&
                               ej["error"].contains("message") &&
                               ej["error"]["message"].is_string()) {
                        engine_err = ej["error"]["message"].get<std::string>();
                    }
                }

                for (const auto& vm : valid_map) {
                    final_res.results[vm.idx].ok = false;
                    final_res.results[vm.idx].error = engine_err;
                }
            }

            // final ok if any image ok
            final_res.ok = false;
            for (const auto& r : final_res.results) {
                if (r.ok) { final_res.ok = true; break; }
            }

            std::string body = final_res.to_json();
            send_json(res, final_res.ok ? 200 : 422, request_id, body);
            finish_log(res.status);
            return;
        }
        catch (const EngineClientError& e) {
            for (const auto& p : temp_paths) {
                std::error_code rm_ec;
                std::filesystem::remove(p, rm_ec);
            }
            int status = e.status_code();
            if (status < 400 || status > 599) status = 502;
            const std::string msg = extract_engine_error_message(e);
            send_json(res, status, request_id,
                      make_error_json(request_id, "ENGINE_ERROR", msg));
            finish_log(res.status);
            return;
        }
        catch (const std::exception& e) {
            for (const auto& p : temp_paths) {
                std::error_code rm_ec;
                std::filesystem::remove(p, rm_ec);
            }
            std::cerr << "[REQ " << request_id << "] INTERNAL_ERROR: " << e.what() << "\n";
            send_json(res, 500, request_id,
                      make_error_json(request_id, "INTERNAL_ERROR", "Internal server error"));
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
