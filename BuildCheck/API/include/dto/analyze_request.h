#pragma once
#include <string>
#include <vector>

struct AnalyzeImageInput {
    std::string filename;
    std::string content_type;     // optional
    std::string bytes;            // raw file bytes
};

struct AnalyzeRequest {
    std::string vehicle_type;
    int year = 0;
    std::vector<AnalyzeImageInput> images;
};
