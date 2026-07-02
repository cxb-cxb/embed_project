#include "cart.hpp"
#include "catalog.hpp"
#include "payment.hpp"
#include "recognizer.hpp"
#include "voice.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void test_catalog() {
    auto catalog = Catalog::loadDefault();
    expect(catalog.size() >= 10, "catalog should contain at least 10 products");
    const Product* water = catalog.findByBarcode("690100000001");
    expect(water != nullptr, "barcode should find mineral water");
    expect(water->id == "mineral_water", "barcode should map to mineral_water");
}

void test_recognition() {
    auto catalog = Catalog::loadDefault();
    RetailRecognizer recognizer;

    auto byBarcode = recognizer.recognize({ "690100000002", "", "" }, catalog);
    expect(byBarcode.has_value(), "barcode recognition should return a product");
    expect(byBarcode->product.id == "cola", "barcode should recognize cola");

    auto byQr = recognizer.recognize({ "", "product:instant_noodles", "" }, catalog);
    expect(byQr.has_value(), "qr recognition should return a product");
    expect(byQr->product.id == "instant_noodles", "qr should recognize noodles");

    auto byVisual = recognizer.recognize({ "", "", "red soda can" }, catalog);
    expect(byVisual.has_value(), "visual keyword should return a product");
    expect(byVisual->product.id == "cola", "visual keyword should recognize cola");
}

void test_cart_and_payment() {
    auto catalog = Catalog::loadDefault();
    Cart cart;
    cart.add(*catalog.findById("cola"), 2);
    cart.add(*catalog.findById("bread"), 1);

    expect(cart.itemCount() == 3, "cart item count should include quantities");
    expect(cart.totalCents() == 2 * 350 + 480, "cart total should match product prices");

    auto request = PaymentGenerator::create(cart.snapshot(), "student-team");
    expect(request.amount_cents == cart.totalCents(), "payment amount should match cart");
    expect(request.url.find("amount=1180") != std::string::npos, "payment url should contain amount");
    expect(request.url.find("order=") != std::string::npos, "payment url should contain order id");
}

void test_voice() {
    auto catalog = Catalog::loadDefault();
    auto add = VoiceInterpreter::parse("add cola", catalog);
    expect(add.type == VoiceActionType::AddProduct, "voice add command should be parsed");
    expect(add.product_id == "cola", "voice add command should identify cola");

    auto query = VoiceInterpreter::parse("price milk", catalog);
    expect(query.type == VoiceActionType::QueryPrice, "voice price command should be parsed");
    expect(query.product_id == "milk", "voice price command should identify milk");

    auto checkout = VoiceInterpreter::parse("checkout", catalog);
    expect(checkout.type == VoiceActionType::Checkout, "voice checkout should be parsed");
}

}  // namespace

int main() {
    try {
        test_catalog();
        test_recognition();
        test_cart_and_payment();
        test_voice();
    } catch (const std::exception& ex) {
        std::cerr << "TEST FAILED: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "All retail core tests passed.\n";
    return EXIT_SUCCESS;
}
