#pragma once

#include "product.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CartLine {
    Product product;
    int quantity = 0;
};

struct OrderSnapshot {
    std::vector<CartLine> lines;
    std::int64_t total_cents = 0;
};

class Cart {
public:
    void add(const Product& product, int quantity = 1);
    bool removeOne(const std::string& product_id);
    void clear();

    int itemCount() const;
    std::int64_t totalCents() const;
    bool empty() const;
    OrderSnapshot snapshot() const;

private:
    std::map<std::string, CartLine> lines_;
};
