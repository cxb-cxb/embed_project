#pragma once

#include "cart.hpp"
#include "catalog.hpp"

#include <string>

class AnswerEngine {
public:
    static std::string answer(const std::string& question, const Catalog& catalog, const Cart& cart);
    static bool isFallbackAnswer(const std::string& answer);
};
