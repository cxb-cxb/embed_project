#pragma once

#include "catalog.hpp"
#include "product.hpp"

#include <optional>
#include <string>

struct RecognitionInput {
    std::string barcode;
    std::string qr_payload;
    std::string visual_label;
};

struct RecognitionResult {
    Product product;
    float confidence = 0.0f;
    std::string source;
};

class RetailRecognizer {
public:
    std::optional<RecognitionResult> recognize(const RecognitionInput& input, const Catalog& catalog) const;
};
