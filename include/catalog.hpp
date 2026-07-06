#pragma once

#include "product.hpp"

#include <cstddef>
#include <string>
#include <vector>

class Catalog {
public:
    static Catalog loadDefault();
    static Catalog loadCsv(const std::string& path);

    void add(Product product);
    const Product* findById(const std::string& id) const;
    const Product* findByBarcode(const std::string& barcode) const;
    const Product* findByVisualText(const std::string& text) const;

    std::size_t size() const;
    const std::vector<Product>& products() const;

private:
    std::vector<Product> products_;
};
