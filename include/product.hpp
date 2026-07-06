#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Product {
    std::string id;
    std::string display_name;
    std::string barcode;
    std::int64_t price_cents = 0;
    std::vector<std::string> visual_keywords;
};
