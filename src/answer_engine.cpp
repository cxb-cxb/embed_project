#include "answer_engine.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string money(std::int64_t cents) {
    std::ostringstream stream;
    stream << "CNY " << (cents / 100) << "." << std::setw(2) << std::setfill('0') << (cents % 100);
    return stream.str();
}

bool asksAboutCart(const std::string& text) {
    return contains(text, "cart") || contains(text, "购物车") || contains(text, "gouwuche") ||
           contains(text, "当前") || contains(text, "总价");
}

bool asksAboutCheckout(const std::string& text) {
    return contains(text, "checkout") || contains(text, "pay") || contains(text, "结算") ||
           contains(text, "支付") || contains(text, "jiesuan");
}

bool asksAboutContest(const std::string& text) {
    return contains(text, "赛题") || contains(text, "比赛") || contains(text, "要求") ||
           contains(text, "功能") || contains(text, "语音提示") || contains(text, "help");
}

bool asksAboutProducts(const std::string& text) {
    return contains(text, "product") || contains(text, "goods") || contains(text, "item") ||
           contains(text, "商品") || contains(text, "有什么") || contains(text, "有哪些") ||
           contains(text, "卖什么") || contains(text, "list");
}

bool asksAboutPrice(const std::string& text) {
    return contains(text, "price") || contains(text, "多少钱") || contains(text, "价格") ||
           contains(text, "jiage") || contains(text, "how much");
}

bool mentionsChineseAlias(const std::string& text, const std::string& product_id) {
    if (product_id == "mineral_water") {
        return contains(text, "水") || contains(text, "矿泉水");
    }
    if (product_id == "cola") {
        return contains(text, "可乐");
    }
    if (product_id == "milk") {
        return contains(text, "牛奶");
    }
    if (product_id == "bread") {
        return contains(text, "面包");
    }
    if (product_id == "instant_noodles") {
        return contains(text, "泡面") || contains(text, "方便面");
    }
    if (product_id == "chips") {
        return contains(text, "薯片");
    }
    if (product_id == "coffee") {
        return contains(text, "咖啡");
    }
    if (product_id == "tea") {
        return contains(text, "茶");
    }
    if (product_id == "cookies") {
        return contains(text, "饼干");
    }
    if (product_id == "yogurt") {
        return contains(text, "酸奶");
    }
    return false;
}

const Product* findMentionedProduct(const std::string& text, const Catalog& catalog) {
    const auto ascii_text = lower(text);
    for (const auto& product : catalog.products()) {
        if (contains(ascii_text, lower(product.id)) || contains(ascii_text, lower(product.display_name)) ||
            mentionsChineseAlias(text, product.id)) {
            return &product;
        }
        for (const auto& keyword : product.visual_keywords) {
            if (contains(ascii_text, lower(keyword))) {
                return &product;
            }
        }
    }
    return nullptr;
}

std::string answerContestIntro() {
    return "Voice prompt: this QSM368ZP-WF smart retail demo supports camera scan, cart total, "
           "payment link generation, edge AI product recognition hooks, microphone voice input, and TTS output hooks.";
}

std::string answerCart(const Cart& cart) {
    if (cart.empty()) {
        return "Cart is empty. Please scan a product or use a voice command to add one.";
    }

    std::ostringstream stream;
    stream << "Current cart: ";
    auto snapshot = cart.snapshot();
    for (std::size_t i = 0; i < snapshot.lines.size(); ++i) {
        const auto& line = snapshot.lines[i];
        if (i > 0) {
            stream << ", ";
        }
        stream << line.product.display_name << " x " << line.quantity;
    }
    stream << ". Total: " << money(snapshot.total_cents) << ".";
    return stream.str();
}

std::string answerProducts(const Catalog& catalog) {
    std::ostringstream stream;
    stream << "Available products: ";
    for (std::size_t i = 0; i < catalog.products().size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << catalog.products()[i].display_name << " (" << money(catalog.products()[i].price_cents) << ")";
    }
    stream << ".";
    return stream.str();
}

}  // namespace

std::string AnswerEngine::answer(const std::string& question, const Catalog& catalog, const Cart& cart) {
    const auto text = lower(question);
    const auto* product = findMentionedProduct(question, catalog);

    if (asksAboutPrice(text) && product != nullptr) {
        return product->display_name + " price is " + money(product->price_cents) + ".";
    }
    if (asksAboutCart(question) || asksAboutCart(text)) {
        return answerCart(cart);
    }
    if (asksAboutProducts(question) || asksAboutProducts(text)) {
        return answerProducts(catalog);
    }
    if (asksAboutCheckout(question) || asksAboutCheckout(text)) {
        return "Checkout creates an order and payment link from the cart. It can be replaced with a real payment API.";
    }
    if (asksAboutContest(question) || asksAboutContest(text)) {
        return answerContestIntro();
    }
    return "I did not understand. Try asking: voice prompt, contest requirements, product price, current cart, or checkout.";
}

bool AnswerEngine::isFallbackAnswer(const std::string& answer) {
    return answer.find("I did not understand.") == 0;
}
