#include "routes/register_routes.h"
#include "routes/analyze_route.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
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

std::string trim_copy(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
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

std::string admin_token() {
    const char* env = std::getenv("BUILDCHECK_CONTACT_ADMIN_TOKEN");
    if (!env) return "";
    return trim_copy(env);
}

bool is_admin_authorized(const httplib::Request& req) {
    const std::string token = admin_token();
    if (token.empty()) return false;
    const std::string header = trim_copy(req.get_header_value("X-Admin-Token"));
    return !header.empty() && header == token;
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

void persist_contacts_locked() {
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

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return;
    const std::string dumped = out.dump();
    f.write(dumped.data(), static_cast<std::streamsize>(dumped.size()));
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
} // namespace

void register_routes(httplib::Server& server, const EngineClient& engine) {
    {
        std::lock_guard<std::mutex> lock(g_contact_mutex);
        load_contacts_if_needed_locked();
    }

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(R"({"status":"ok","service":"BuildCheck API"})", "application/json");
    });

    server.Options("/api/contact", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    server.Options("/api/admin/contact/submissions", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token");
        res.status = 204;
    });

    server.Post("/api/contact", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");

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
            persist_contacts_locked();
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

    server.Get("/api/admin/contact/submissions", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        if (admin_token().empty()) {
            res.status = 503;
            res.set_content(json{{"ok", false}, {"error", {{"code", "ADMIN_NOT_CONFIGURED"}, {"message", "Admin token is not configured"}}}}.dump(), "application/json");
            return;
        }
        if (!is_admin_authorized(req)) {
            res.status = 401;
            res.set_content(json{{"ok", false}, {"error", {{"code", "UNAUTHORIZED"}, {"message", "Invalid admin token"}}}}.dump(), "application/json");
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
