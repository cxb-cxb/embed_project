#include "payment.hpp"

#include <chrono>
#include <sstream>

PaymentRequest PaymentGenerator::create(const OrderSnapshot& order, const std::string& merchant_id) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::ostringstream orderId;
    orderId << merchant_id << "-" << millis;

    std::ostringstream url;
    url << "https://pay.example.local/qsm368?merchant=" << merchant_id
        << "&order=" << orderId.str()
        << "&amount=" << order.total_cents;

    return PaymentRequest{orderId.str(), order.total_cents, url.str()};
}
