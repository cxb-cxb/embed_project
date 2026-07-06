#pragma once

#include "cart.hpp"

#include <cstdint>
#include <string>

struct PaymentRequest {
    std::string order_id;
    std::int64_t amount_cents = 0;
    std::string url;
};

class PaymentGenerator {
public:
    static PaymentRequest create(const OrderSnapshot& order, const std::string& merchant_id);
};
