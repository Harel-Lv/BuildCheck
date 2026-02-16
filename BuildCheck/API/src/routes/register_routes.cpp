#include "routes/register_routes.h"
#include "routes/analyze_route.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "third_party/json.hpp"

namespace {
using nlohmann::json;

struct ContactEntry {
    std::string name;
    std::string phone;
    std::string message;
    std::string registered_at;
};

std::mutex g_contact_mutex;
std::vector<ContactEntry> g_contact_entries;
constexpr std::size_t kMaxContactEntries = 1000;
bool g_contact_loaded = false;
std::unordered_map<std::string, long long> g_admin_sessions;
bool g_admin_sessions_loaded = false;
constexpr int kAdminSessionMaxAgeSec = 8 * 60 * 60;

bool persist_contacts_locked();
bool persist_admin_sessions_locked();

std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32]{0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

json make_contact_error(const std::string& message) {
    return json{
        {"ok", false},
        {"error", {{"code", "INVALID_CONTACT"}, {"message", message}}}
    };
}

std::string contact_db_path() {
    const char* env = std::getenv("BUILDCHECK_CONTACT_DB_PATH");
    if (env && *env) return std::string(env);
    const char* shared_tmp = std::getenv("BUILDCHECK_SHARED_TMP");
    if (shared_tmp && *shared_tmp) {
        std::filesystem::path p = std::filesystem::path(shared_tmp) / "contact_submissions.json";
        return p.string();
    }
    std::filesystem::path p = std::filesystem::temp_directory_path() / "buildcheck_contact_submissions.json";
    return p.string();
}

std::string admin_sessions_db_path() {
    const char* env = std::getenv("BUILDCHECK_ADMIN_SESSION_DB_PATH");
    if (env && *env) return std::string(env);
    return contact_db_path() + ".sessions.json";
}

std::string admin_token() {
    const char* env = std::getenv("BUILDCHECK_CONTACT_ADMIN_TOKEN");
    if (!env) return "";
    return trim_copy(env);
}

std::string admin_username() {
    const char* env = std::getenv("BUILDCHECK_ADMIN_USERNAME");
    if (!env) return "";
    return trim_copy(env);
}

std::string admin_password() {
    const char* env = std::getenv("BUILDCHECK_ADMIN_PASSWORD");
    if (!env) return "";
    return trim_copy(env);
}

std::vector<std::string> split_csv(const std::string& raw) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= raw.size()) {
        std::size_t end = raw.find(',', start);
        if (end == std::string::npos) end = raw.size();
        std::string token = trim_copy(raw.substr(start, end - start));
        if (!token.empty()) out.push_back(token);
        if (end == raw.size()) break;
        start = end + 1;
    }
    return out;
}

bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string random_hex(std::size_t bytes_len) {
    std::random_device rd;
    const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes_len * 2);
    for (std::size_t i = 0; i < bytes_len; ++i) {
        const unsigned int v = rd() & 0xFFu;
        out.push_back(hex[(v >> 4) & 0x0F]);
        out.push_back(hex[v & 0x0F]);
    }
    return out;
}

std::string cookie_value(const httplib::Request& req, const std::string& name) {
    const std::string cookie = req.get_header_value("Cookie");
    if (cookie.empty()) return "";

    const std::string key = name + "=";
    std::size_t start = 0;
    while (start < cookie.size()) {
        std::size_t end = cookie.find(';', start);
        if (end == std::string::npos) end = cookie.size();
        std::string part = trim_copy(cookie.substr(start, end - start));
        if (part.rfind(key, 0) == 0) {
            return part.substr(key.size());
        }
        start = end + 1;
    }
    return "";
}

bool should_set_secure_cookie(const httplib::Request& req) {
    const char* secure_env = std::getenv("BUILDCHECK_COOKIE_SECURE");
    if (secure_env && *secure_env) {
        const std::string v = to_lower(trim_copy(secure_env));
        if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    }
    const std::string proto = to_lower(trim_copy(req.get_header_value("X-Forwarded-Proto")));
    return proto == "https";
}

std::string session_cookie_header(const std::string& session_id, int max_age_sec, bool secure) {
    std::string v = "buildcheck_admin_session=" + session_id +
                    "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + std::to_string(max_age_sec);
    if (secure) v += "; Secure";
    return v;
}

bool cleanup_expired_sessions_locked() {
    const long long now = static_cast<long long>(std::time(nullptr));
    std::vector<std::string> stale;
    stale.reserve(g_admin_sessions.size());
    for (const auto& kv : g_admin_sessions) {
        if (kv.second <= now) stale.push_back(kv.first);
    }
    if (stale.empty()) return false;
    for (const auto& id : stale) g_admin_sessions.erase(id);
    return true;
}

bool is_admin_session_valid(const httplib::Request& req) {
    const std::string session_id = cookie_value(req, "buildcheck_admin_session");
    if (session_id.empty()) return false;
    std::lock_guard<std::mutex> lock(g_contact_mutex);
    if (!g_admin_sessions_loaded) {
        const std::string path = admin_sessions_db_path();
        std::ifstream in(path, std::ios::binary);
        if (in.is_open()) {
            const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            json payload = json::parse(raw, nullptr, false);
            if (!payload.is_discarded() && payload.is_object()) {
                g_admin_sessions.clear();
                for (auto it = payload.begin(); it != payload.end(); ++it) {
                    if (it.value().is_number_integer()) {
                        g_admin_sessions[it.key()] = it.value().get<long long>();
                    }
                }
            }
        }
        g_admin_sessions_loaded = true;
    }
    const bool removed_any = cleanup_expired_sessions_locked();
    if (removed_any) {
        (void)persist_admin_sessions_locked();
    }
    auto it = g_admin_sessions.find(session_id);
    if (it == g_admin_sessions.end()) return false;
    const long long now = static_cast<long long>(std::time(nullptr));
    if (it->second <= now) {
        g_admin_sessions.erase(it);
        (void)persist_admin_sessions_locked();
        return false;
    }
    return true;
}

bool is_admin_authorized(const httplib::Request& req) {
    if (is_admin_session_valid(req)) return true;

    const std::string token = admin_token();
    if (!token.empty()) {
        const std::string header = trim_copy(req.get_header_value("X-Admin-Token"));
        if (!header.empty() && constant_time_equals(header, token)) return true;
    }
    return false;
}

void load_contacts_if_needed_locked() {
    if (g_contact_loaded) return;
    g_contact_loaded = true;

    const std::string path = contact_db_path();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return;

    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json payload = json::parse(raw, nullptr, false);
    if (payload.is_discarded() || !payload.is_array()) return;

    g_contact_entries.clear();
    for (const auto& item : payload) {
        if (!item.is_object()) continue;
        g_contact_entries.push_back(ContactEntry{
            item.value("name", ""),
            item.value("phone", ""),
            item.value("message", ""),
            item.value("registered_at", "")
        });
        if (g_contact_entries.size() >= kMaxContactEntries) break;
    }
}

bool persist_contacts_locked() {
    const std::string path = contact_db_path();
    std::filesystem::path p(path);
    std::error_code ec;
    if (!p.parent_path().empty()) std::filesystem::create_directories(p.parent_path(), ec);

    json out = json::array();
    for (const auto& e : g_contact_entries) {
        out.push_back({
            {"name", e.name},
            {"phone", e.phone},
            {"message", e.message},
            {"registered_at", e.registered_at}
        });
    }

    const std::string tmp_path = path + ".tmp";
    std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    const std::string dumped = out.dump();
    f.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
    if (!f.good()) {
        f.close();
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        return false;
    }
    f.flush();
    if (!f.good()) {
        f.close();
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        return false;
    }
    f.close();
    std::error_code mv_ec;
    std::filesystem::rename(tmp_path, path, mv_ec);
    if (mv_ec) {
        std::filesystem::remove(path, mv_ec);
        mv_ec.clear();
        std::filesystem::rename(tmp_path, path, mv_ec);
        if (mv_ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_path, rm_ec);
            return false;
        }
    }
    return true;
}

bool persist_admin_sessions_locked() {
    const std::string path = admin_sessions_db_path();
    std::filesystem::path p(path);
    std::error_code ec;
    if (!p.parent_path().empty()) std::filesystem::create_directories(p.parent_path(), ec);

    json payload = json::object();
    for (const auto& kv : g_admin_sessions) payload[kv.first] = kv.second;

    const std::string tmp_path = path + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    const std::string dumped = payload.dump();
    out.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
    if (!out.good()) {
        out.close();
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        return false;
    }
    out.flush();
    if (!out.good()) {
        out.close();
        std::error_code rm_ec;
        std::filesystem::remove(tmp_path, rm_ec);
        return false;
    }
    out.close();

    std::error_code mv_ec;
    std::filesystem::rename(tmp_path, path, mv_ec);
    if (mv_ec) {
        std::filesystem::remove(path, mv_ec);
        mv_ec.clear();
        std::filesystem::rename(tmp_path, path, mv_ec);
        if (mv_ec) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp_path, rm_ec);
            return false;
        }
    }
    return true;
}

bool validate_contact(const std::string& name,
                      const std::string& phone,
                      const std::string& message,
                      std::string& error_msg) {
    if (name.size() < 2 || name.size() > 80) {
        error_msg = "Name must be 2-80 characters";
        return false;
    }
    static const std::regex phone_re(R"(^\+?[0-9()\-\s]{7,20}$)");
    if (!std::regex_match(phone, phone_re)) {
        error_msg = "Phone format is invalid";
        return false;
    }
    if (message.size() < 5 || message.size() > 2000) {
        error_msg = "Message must be 5-2000 characters";
        return false;
    }
    return true;
}

void set_cors_public(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
}

void set_cors_admin(const httplib::Request& req,
                    httplib::Response& res,
                    const std::string& methods,
                    const std::string& headers) {
    const std::string origin = trim_copy(req.get_header_value("Origin"));
    if (!origin.empty()) {
        bool allowed = false;
        const char* env = std::getenv("BUILDCHECK_ADMIN_ALLOWED_ORIGINS");
        if (env && *env) {
            const auto allowed_origins = split_csv(env);
            for (const auto& o : allowed_origins) {
                if (constant_time_equals(origin, o)) {
                    allowed = true;
                    break;
                }
            }
        } else {
            allowed = (origin == "http://127.0.0.1:8080" ||
                       origin == "http://localhost:8080" ||
                       origin == "http://127.0.0.1:8081" ||
                       origin == "http://localhost:8081");
        }

        if (allowed) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Access-Control-Allow-Credentials", "true");
            res.set_header("Vary", "Origin");
        } else {
            res.set_header("Vary", "Origin");
        }
    } else {
        // Same-origin or non-browser clients.
        res.set_header("Access-Control-Allow-Origin", "*");
    }
    res.set_header("Access-Control-Allow-Methods", methods);
    res.set_header("Access-Control-Allow-Headers", headers);
}
} // namespace

void register_routes(httplib::Server& server, const EngineClient& engine) {
    {
        std::lock_guard<std::mutex> lock(g_contact_mutex);
        load_contacts_if_needed_locked();
    }

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        set_cors_public(res);
        res.set_content(R"({"status":"ok","service":"BuildCheck API"})", "application/json");
    });

    server.Options("/api/contact", [](const httplib::Request&, httplib::Response& res) {
        set_cors_public(res);
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    server.Options("/api/admin/contact/submissions", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "GET, OPTIONS", "Content-Type, X-Admin-Token");
        res.status = 204;
    });

    server.Options("/api/admin/login", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "POST, OPTIONS", "Content-Type");
        res.status = 204;
    });

    server.Options("/api/admin/logout", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "POST, OPTIONS", "Content-Type");
        res.status = 204;
    });

    server.Post("/api/contact", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_public(res);

        json payload = json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            res.status = 400;
            res.set_content(make_contact_error("Expected JSON body").dump(), "application/json");
            return;
        }

        const std::string name = trim_copy(payload.value("name", ""));
        const std::string phone = trim_copy(payload.value("phone", ""));
        const std::string message = trim_copy(payload.value("message", ""));
        std::string error_msg;
        if (!validate_contact(name, phone, message, error_msg)) {
            res.status = 400;
            res.set_content(make_contact_error(error_msg).dump(), "application/json");
            return;
        }

        ContactEntry entry{name, phone, message, now_utc_iso8601()};
        {
            std::lock_guard<std::mutex> lock(g_contact_mutex);
            load_contacts_if_needed_locked();
            if (g_contact_entries.size() >= kMaxContactEntries) {
                g_contact_entries.erase(g_contact_entries.begin());
            }
            g_contact_entries.push_back(entry);
            if (!persist_contacts_locked()) {
                res.status = 500;
                res.set_content(json{{"ok", false}, {"error", {{"code", "PERSISTENCE_ERROR"}, {"message", "Failed to persist contact submission"}}}}.dump(), "application/json");
                return;
            }
        }

        res.status = 201;
        res.set_content(
            json{
                {"ok", true},
                {"item", {
                    {"name", entry.name},
                    {"phone", entry.phone},
                    {"message", entry.message},
                    {"registered_at", entry.registered_at}
                }}
            }.dump(),
            "application/json"
        );
    });

    server.Post("/api/admin/login", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "POST, OPTIONS", "Content-Type");

        const std::string user = admin_username();
        const std::string pass = admin_password();
        if (user.empty() || pass.empty()) {
            res.status = 503;
            res.set_content(json{{"ok", false}, {"error", {{"code", "ADMIN_NOT_CONFIGURED"}, {"message", "Admin username/password not configured"}}}}.dump(), "application/json");
            return;
        }

        json payload = json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            res.status = 400;
            res.set_content(json{{"ok", false}, {"error", {{"code", "BAD_REQUEST"}, {"message", "Expected JSON body"}}}}.dump(), "application/json");
            return;
        }

        const std::string in_user = trim_copy(payload.value("username", ""));
        const std::string in_pass = trim_copy(payload.value("password", ""));
        if (!constant_time_equals(in_user, user) || !constant_time_equals(in_pass, pass)) {
            res.status = 401;
            res.set_content(json{{"ok", false}, {"error", {{"code", "UNAUTHORIZED"}, {"message", "Invalid credentials"}}}}.dump(), "application/json");
            return;
        }

        const std::string session_id = random_hex(24);
        {
            std::lock_guard<std::mutex> lock(g_contact_mutex);
            cleanup_expired_sessions_locked();
            const long long now = static_cast<long long>(std::time(nullptr));
            g_admin_sessions[session_id] = now + kAdminSessionMaxAgeSec;
            if (!persist_admin_sessions_locked()) {
                g_admin_sessions.erase(session_id);
                res.status = 500;
                res.set_content(json{{"ok", false}, {"error", {{"code", "PERSISTENCE_ERROR"}, {"message", "Failed to persist admin session"}}}}.dump(), "application/json");
                return;
            }
        }

        res.set_header("Set-Cookie", session_cookie_header(session_id, kAdminSessionMaxAgeSec, should_set_secure_cookie(req)));
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    server.Post("/api/admin/logout", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "POST, OPTIONS", "Content-Type");
        const std::string session_id = cookie_value(req, "buildcheck_admin_session");
        if (!session_id.empty()) {
            std::lock_guard<std::mutex> lock(g_contact_mutex);
            g_admin_sessions.erase(session_id);
            if (!persist_admin_sessions_locked()) {
                res.status = 500;
                res.set_content(json{{"ok", false}, {"error", {{"code", "PERSISTENCE_ERROR"}, {"message", "Failed to persist admin session state"}}}}.dump(), "application/json");
                return;
            }
        }
        res.set_header("Set-Cookie", session_cookie_header("", 0, should_set_secure_cookie(req)));
        res.set_content(json{{"ok", true}}.dump(), "application/json");
    });

    server.Get("/api/admin/contact/submissions", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_admin(req, res, "GET, OPTIONS", "Content-Type, X-Admin-Token");
        if ((admin_username().empty() || admin_password().empty()) && admin_token().empty()) {
            res.status = 503;
            res.set_content(json{{"ok", false}, {"error", {{"code", "ADMIN_NOT_CONFIGURED"}, {"message", "Admin auth is not configured"}}}}.dump(), "application/json");
            return;
        }
        if (!is_admin_authorized(req)) {
            res.status = 401;
            res.set_content(json{{"ok", false}, {"error", {{"code", "UNAUTHORIZED"}, {"message", "Unauthorized"}}}}.dump(), "application/json");
            return;
        }

        json items = json::array();
        {
            std::lock_guard<std::mutex> lock(g_contact_mutex);
            load_contacts_if_needed_locked();
            for (auto it = g_contact_entries.rbegin(); it != g_contact_entries.rend(); ++it) {
                items.push_back({
                    {"name", it->name},
                    {"phone", it->phone},
                    {"message", it->message},
                    {"registered_at", it->registered_at}
                });
            }
        }
        res.set_content(json{{"ok", true}, {"items", items}}.dump(), "application/json");
    });

    register_analyze_route(server, engine);
}
