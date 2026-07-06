#include "voice.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

const Product* findMentionedProduct(const std::string& command, const Catalog& catalog) {
    for (const auto& product : catalog.products()) {
        if (contains(command, lower(product.id)) || contains(command, lower(product.display_name))) {
            return &product;
        }
        for (const auto& keyword : product.visual_keywords) {
            if (contains(command, lower(keyword))) {
                return &product;
            }
        }
    }
    return nullptr;
}

}  // namespace

VoiceAction VoiceInterpreter::parse(const std::string& command, const Catalog& catalog) {
    const auto text = lower(command);
    const auto* product = findMentionedProduct(text, catalog);

    if (contains(text, "checkout") || contains(text, "pay") || contains(text, "jiesuan")) {
        return VoiceAction{VoiceActionType::Checkout, ""};
    }
    if (contains(text, "clear") || contains(text, "empty") || contains(text, "qingkong")) {
        return VoiceAction{VoiceActionType::ClearCart, ""};
    }
    if (contains(text, "cart") || contains(text, "show") || contains(text, "gouwuche")) {
        return VoiceAction{VoiceActionType::ShowCart, ""};
    }
    if ((contains(text, "price") || contains(text, "how much") || contains(text, "jiage")) && product != nullptr) {
        return VoiceAction{VoiceActionType::QueryPrice, product->id};
    }
    if ((contains(text, "add") || contains(text, "buy") || contains(text, "scan") || contains(text, "jiaru")) &&
        product != nullptr) {
        return VoiceAction{VoiceActionType::AddProduct, product->id};
    }

    return VoiceAction{VoiceActionType::Unknown, product != nullptr ? product->id : ""};
}
