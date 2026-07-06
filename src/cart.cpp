#include "cart.hpp"

#include <stdexcept>

void Cart::add(const Product& product, int quantity) {
    if (quantity <= 0) {
        throw std::invalid_argument("quantity must be positive");
    }

    auto& line = lines_[product.id];
    if (line.quantity == 0) {
        line.product = product;
    }
    line.quantity += quantity;
}

bool Cart::removeOne(const std::string& product_id) {
    auto iter = lines_.find(product_id);
    if (iter == lines_.end()) {
        return false;
    }
    iter->second.quantity -= 1;
    if (iter->second.quantity <= 0) {
        lines_.erase(iter);
    }
    return true;
}

void Cart::clear() {
    lines_.clear();
}

int Cart::itemCount() const {
    int count = 0;
    for (const auto& pair : lines_) {
        count += pair.second.quantity;
    }
    return count;
}

std::int64_t Cart::totalCents() const {
    std::int64_t total = 0;
    for (const auto& pair : lines_) {
        total += pair.second.product.price_cents * pair.second.quantity;
    }
    return total;
}

bool Cart::empty() const {
    return lines_.empty();
}

OrderSnapshot Cart::snapshot() const {
    OrderSnapshot snapshot;
    for (const auto& pair : lines_) {
        snapshot.lines.push_back(pair.second);
    }
    snapshot.total_cents = totalCents();
    return snapshot;
}
