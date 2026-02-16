#pragma once
#include <string>
#include <vector>
#include <sstream>

struct AnalyzeImageResult {
    std::string filename;
    bool ok = false;

    // Output fields (placeholder now, real later)
    std::vector<std::string> damage_types;
    int cost_min = 0;
    int cost_max = 0;

    // If ok==false
    std::string error;
};

struct AnalyzeResponse {
    bool ok = false;
    std::string request_id;
    std::vector<AnalyzeImageResult> results;

    // Minimal JSON builder (no external JSON lib)
    static std::string escape_json(const std::string& s) {
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

    std::string to_json() const {
        std::ostringstream os;
        os << R"({"ok":)" << (ok ? "true" : "false")
           << R"(,"request_id":")" << escape_json(request_id) << R"(")"
           << R"(,"results":[)";

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            if (i) os << ",";
            os << R"({"filename":")" << escape_json(r.filename) << R"(")"
               << R"(,"ok":)" << (r.ok ? "true" : "false");

            if (r.ok) {
                os << R"(,"damage_types":[)";
                for (size_t j = 0; j < r.damage_types.size(); ++j) {
                    if (j) os << ",";
                    os << "\"" << escape_json(r.damage_types[j]) << "\"";
                }
                os << R"(],"cost_min":)" << r.cost_min
                   << R"(,"cost_max":)" << r.cost_max;
            } else {
                os << R"(,"error":")" << escape_json(r.error) << R"(")";
            }

            os << "}";
        }

        os << "]}";
        return os.str();
    }
};
