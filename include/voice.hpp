#pragma once

#include "catalog.hpp"

#include <string>

enum class VoiceActionType {
    Unknown,
    AddProduct,
    QueryPrice,
    Checkout,
    ClearCart,
    ShowCart,
};

struct VoiceAction {
    VoiceActionType type = VoiceActionType::Unknown;
    std::string product_id;
};

class VoiceInterpreter {
public:
    static VoiceAction parse(const std::string& command, const Catalog& catalog);
};
