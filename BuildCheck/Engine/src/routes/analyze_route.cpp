#include "utils/httplib.h"
#include <string>
#include <vector>
#include <sstream>
#include <random>

// אם יש לך nlohmann json ב-third_party, תשתמש בו.
// כרגע נפרסר ידנית בצורה בטוחה מינימלית כדי לא להיתקע על תלויות.

static std::string escape_json(const std::string& s) {
    std::string out; out.reserve(s.size());
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

// Parser מינימלי: מחפש "paths":[ ... ] ומחלץ מחרוזות בתוך ""
static std::vector<std::string> extract_paths(const std::string& body) {
    std::vector<std::string> paths;

    const std::string key = "\"paths\"";
    auto kpos = body.find(key);
    if (kpos == std::string::npos) return paths;

    auto lbr = body.find('[', kpos);
    auto rbr = body.find(']', lbr);
    if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr) return paths;

    std::string arr = body.substr(lbr + 1, rbr - lbr - 1);

    // scan "..."
    bool in = false;
    std::string cur;
    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (!in) {
            if (c == '"') { in = true; cur.clear(); }
        } else {
            if (c == '\\' && i + 1 < arr.size()) { // skip escaped char
                cur += arr[i + 1];
                ++i;
            } else if (c == '"') {
                in = false;
                paths.push_back(cur);
            } else {
                cur += c;
            }
        }
    }
    return paths;
}

// Stub: מחזיר damage_types “מזויף” כדי לבדוק צינור End-to-End
static std::string fake_damage_type_for(const std::string& path) {
    static const char* types[] = {"crack","leakage","corrosion","abscission","bulge"};
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(0, 4);
    (void)path;
    return types[dist(rng)];
}

void register_analyze_route(httplib::Server& server) {
    server.Post("/engine/analyze", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        // בינתיים דורשים JSON
        if (req.body.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"empty body"})", "application/json");
            return;
        }

        auto paths = extract_paths(req.body);
        if (paths.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"missing paths array"})", "application/json");
            return;
        }

        std::ostringstream os;
        os << R"({"ok":true,"results":[)";

        for (size_t i = 0; i < paths.size(); ++i) {
            const std::string dtype = fake_damage_type_for(paths[i]);

            os << R"({"ok":true,"path":")" << escape_json(paths[i])
               << R"(","damage_types":[")" << escape_json(dtype) << R"("]})";

            if (i + 1 < paths.size()) os << ",";
        }

        os << "]}";
        res.status = 200;
        res.set_content(os.str(), "application/json");
    });
}
