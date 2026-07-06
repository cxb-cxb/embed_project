#include "catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        auto cleaned = trim(item);
        if (!cleaned.empty()) {
            parts.push_back(cleaned);
        }
    }
    return parts;
}

Product product(std::string id, std::string name, std::string barcode, std::int64_t price,
                std::vector<std::string> keywords) {
    return Product{ std::move(id), std::move(name), std::move(barcode), price, std::move(keywords) };
}

}  // namespace

Catalog Catalog::loadDefault() {
    Catalog catalog;
    catalog.add(product("mineral_water", "Mineral Water", "690100000001", 200, {"water", "bottle", "blue"}));
    catalog.add(product("cola", "Cola", "690100000002", 350, {"cola", "soda", "red", "can"}));
    catalog.add(product("milk", "Milk", "690100000003", 620, {"milk", "carton", "white"}));
    catalog.add(product("bread", "Bread", "690100000004", 480, {"bread", "toast", "bag"}));
    catalog.add(product("instant_noodles", "Instant Noodles", "690100000005", 550, {"noodle", "bowl", "cup"}));
    catalog.add(product("chips", "Potato Chips", "690100000006", 680, {"chips", "potato", "yellow"}));
    catalog.add(product("coffee", "Coffee", "690100000007", 990, {"coffee", "black", "cup"}));
    catalog.add(product("tea", "Tea Drink", "690100000008", 450, {"tea", "green", "bottle"}));
    catalog.add(product("cookies", "Cookies", "690100000009", 720, {"cookie", "cookies", "box"}));
    catalog.add(product("yogurt", "Yogurt", "690100000010", 580, {"yogurt", "cup", "purple"}));
    return catalog;
}

Catalog Catalog::loadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open product catalog: " + path);
    }

    Catalog catalog;
    std::string line;
    bool header = true;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        if (header) {
            header = false;
            if (line.find("id,") == 0) {
                continue;
            }
        }

        auto columns = split(line, ',');
        if (columns.size() < 5) {
            throw std::runtime_error("bad catalog row: " + line);
        }
        catalog.add(Product{columns[0], columns[1], columns[2], std::stoll(columns[3]), split(columns[4], '|')});
    }

    return catalog;
}

void Catalog::add(Product product) {
    products_.push_back(std::move(product));
}

const Product* Catalog::findById(const std::string& id) const {
    const auto wanted = lower(id);
    for (const auto& item : products_) {
        if (lower(item.id) == wanted) {
            return &item;
        }
    }
    return nullptr;
}

const Product* Catalog::findByBarcode(const std::string& barcode) const {
    for (const auto& item : products_) {
        if (item.barcode == barcode) {
            return &item;
        }
    }
    return nullptr;
}

const Product* Catalog::findByVisualText(const std::string& text) const {
    const auto haystack = lower(text);
    for (const auto& item : products_) {
        if (haystack.find(lower(item.id)) != std::string::npos ||
            haystack.find(lower(item.display_name)) != std::string::npos) {
            return &item;
        }
        for (const auto& keyword : item.visual_keywords) {
            if (haystack.find(lower(keyword)) != std::string::npos) {
                return &item;
            }
        }
    }
    return nullptr;
}

std::size_t Catalog::size() const {
    return products_.size();
}

const std::vector<Product>& Catalog::products() const {
    return products_;
}
