#include "recognizer.hpp"

namespace {

std::string productIdFromQr(const std::string& payload) {
    const std::string prefix = "product:";
    if (payload.find(prefix) == 0) {
        return payload.substr(prefix.size());
    }
    return "";
}

}  // namespace

std::optional<RecognitionResult> RetailRecognizer::recognize(const RecognitionInput& input,
                                                             const Catalog& catalog) const {
    if (!input.barcode.empty()) {
        if (const auto* product = catalog.findByBarcode(input.barcode)) {
            return RecognitionResult{*product, 0.99f, "barcode"};
        }
    }

    if (!input.qr_payload.empty()) {
        const auto id = productIdFromQr(input.qr_payload);
        if (!id.empty()) {
            if (const auto* product = catalog.findById(id)) {
                return RecognitionResult{*product, 0.98f, "qr"};
            }
        }
    }

    if (!input.visual_label.empty()) {
        if (const auto* product = catalog.findByVisualText(input.visual_label)) {
            return RecognitionResult{*product, 0.82f, "visual"};
        }
    }

    return std::nullopt;
}
