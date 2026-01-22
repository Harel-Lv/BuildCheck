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
    std::string vehicle_type;
    int year = 0;
    std::vector<AnalyzeImageResult> results;

    // Minimal JSON builder (no external JSON lib)
    static std::string escape_json(const std::string& s) {
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

    std::string to_json() const {
        std::ostringstream os;
        os << R"({"ok":)" << (ok ? "true" : "false")
           << R"(,"vehicle_type":")" << escape_json(vehicle_type) << R"(")"
           << R"(,"year":)" << year
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
