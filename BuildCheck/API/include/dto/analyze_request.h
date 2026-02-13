#pragma once
#include <string>
#include <vector>

struct AnalyzeImageInput {
    std::string filename;
    std::string content_type;     // optional
    std::string bytes;            // raw file bytes
};

struct AnalyzeRequest {
    std::vector<AnalyzeImageInput> images;
};
