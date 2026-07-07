#include "cart.hpp"
#include "catalog.hpp"
#include "payment.hpp"
#include "recognizer.hpp"
#include "voice.hpp"

#include <iomanip>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

std::string money(std::int64_t cents) {
    std::ostringstream stream;
    stream << "CNY " << (cents / 100) << "." << std::setw(2) << std::setfill('0') << (cents % 100);
    return stream.str();
}

void printHelp() {
    std::cout
        << "Commands:\n"
        << "  list                         show product catalog\n"
        << "  scan <barcode>               add product by barcode\n"
        << "  qr product:<product_id>      add product by QR payload\n"
        << "  see <visual text>            add product by visual keyword\n"
        << "  voice <command>              parse voice command, e.g. add cola\n"
        << "  cart                         show cart\n"
        << "  checkout                     create payment link\n"
        << "  clear                        clear cart\n"
        << "  help                         show commands\n"
        << "  exit                         quit\n";
}

void printCatalog(const Catalog& catalog) {
    for (const auto& product : catalog.products()) {
        std::cout << product.id << " | " << product.display_name << " | barcode=" << product.barcode
                  << " | " << money(product.price_cents) << "\n";
    }
}

void printCart(const Cart& cart) {
    auto order = cart.snapshot();
    if (order.lines.empty()) {
        std::cout << "Cart is empty.\n";
        return;
    }

    for (const auto& line : order.lines) {
        std::cout << line.product.display_name << " x " << line.quantity
                  << " = " << money(line.product.price_cents * line.quantity) << "\n";
    }
    std::cout << "Total: " << money(order.total_cents) << "\n";
}

void addRecognitionResult(const std::optional<RecognitionResult>& result, Cart& cart) {
    if (!result.has_value()) {
        std::cout << "No product recognized.\n";
        return;
    }
    cart.add(result->product);
    std::cout << "Added " << result->product.display_name << " by " << result->source
              << ", confidence=" << result->confidence << "\n";
}

void runVoice(const std::string& command, const Catalog& catalog, Cart& cart) {
    const auto action = VoiceInterpreter::parse(command, catalog);
    switch (action.type) {
        case VoiceActionType::AddProduct: {
            const auto* product = catalog.findById(action.product_id);
            if (product != nullptr) {
                cart.add(*product);
                std::cout << "Voice added " << product->display_name << "\n";
            }
            break;
        }
        case VoiceActionType::QueryPrice: {
            const auto* product = catalog.findById(action.product_id);
            if (product != nullptr) {
                std::cout << product->display_name << " price is " << money(product->price_cents) << "\n";
            }
            break;
        }
        case VoiceActionType::Checkout:
            std::cout << "Voice requested checkout.\n";
            break;
        case VoiceActionType::ClearCart:
            cart.clear();
            std::cout << "Cart cleared.\n";
            break;
        case VoiceActionType::ShowCart:
            printCart(cart);
            break;
        case VoiceActionType::Unknown:
            std::cout << "Unknown voice command.\n";
            break;
    }
}

Catalog loadCatalog(int argc, char** argv) {
    if (argc > 1) {
        return Catalog::loadCsv(argv[1]);
    }

    const char* candidates[] = {
        "/userdata/Embed_project/data/products.csv",
        "data/products.csv",
    };
    for (const char* path : candidates) {
        std::ifstream file(path);
        if (file.good()) {
            return Catalog::loadCsv(path);
        }
    }
    return Catalog::loadDefault();
}

}  // namespace

int main(int argc, char** argv) {
    Catalog catalog;
    try {
        catalog = loadCatalog(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "Failed to load catalog: " << ex.what() << "\n";
        return 1;
    }

    Cart cart;
    RetailRecognizer recognizer;

    std::cout << "Embed_project Smart Retail Demo\n";
    printHelp();

    std::string line;
    while (std::cout << "\n> " && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "exit" || command == "quit") {
            break;
        } else if (command == "help") {
            printHelp();
        } else if (command == "list") {
            printCatalog(catalog);
        } else if (command == "scan") {
            std::string barcode;
            input >> barcode;
            addRecognitionResult(recognizer.recognize({barcode, "", ""}, catalog), cart);
        } else if (command == "qr") {
            std::string payload;
            input >> payload;
            addRecognitionResult(recognizer.recognize({"", payload, ""}, catalog), cart);
        } else if (command == "see") {
            std::string text;
            std::getline(input, text);
            addRecognitionResult(recognizer.recognize({"", "", text}, catalog), cart);
        } else if (command == "voice") {
            std::string text;
            std::getline(input, text);
            runVoice(text, catalog, cart);
        } else if (command == "cart") {
            printCart(cart);
        } else if (command == "checkout") {
            if (cart.empty()) {
                std::cout << "Cart is empty.\n";
            } else {
                auto request = PaymentGenerator::create(cart.snapshot(), "student-team");
                std::cout << "Order: " << request.order_id << "\n";
                std::cout << "Amount: " << money(request.amount_cents) << "\n";
                std::cout << "Pay URL: " << request.url << "\n";
            }
        } else if (command == "clear") {
            cart.clear();
            std::cout << "Cart cleared.\n";
        } else if (!command.empty()) {
            std::cout << "Unknown command. Type help.\n";
        }
    }

    return 0;
}
