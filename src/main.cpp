#include "answer_engine.hpp"
#include "cart.hpp"
#include "catalog.hpp"
#include "cloud_asr.hpp"
#include "cloud_llm.hpp"
#include "cloud_tts.hpp"
#include "microphone_driver.hpp"
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
        << "  ask <question>               answer contest or retail questions in terminal\n"
        << "  mic <wav> [seconds]          record microphone audio with tinycap on board Linux\n"
        << "  voiceask [seconds]           record, recognize by cloud ASR, then answer\n"
        << "  listen                       continuous voice questions until Ctrl+C\n"
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

void runMicrophoneCapture(std::istringstream& input) {
    MicrophoneCaptureConfig config;
    input >> config.output_wav;
    if (config.output_wav.empty()) {
        config.output_wav = "/tmp/embed_question.wav";
    }
    if (!(input >> config.seconds)) {
        config.seconds = 3;
    }

    std::cout << "Recording command: " << MicrophoneDriver::buildRecordCommand(config) << "\n";
    if (MicrophoneDriver::recordToFile(config)) {
        std::cout << "Microphone audio saved to " << config.output_wav << "\n";
    } else {
        std::cout << "Microphone recording failed. Check arecord, audio device, and permissions.\n";
    }
}

std::string answerWithLocalThenLlm(const std::string& question, const Catalog& catalog, const Cart& cart) {
    const auto local_answer = AnswerEngine::answer(question, catalog, cart);
    if (!AnswerEngine::isFallbackAnswer(local_answer)) {
        return local_answer;
    }

    const auto llm = CloudLlmClient::loadFromEnvironment();
    if (!CloudLlmClient::isConfigured(llm)) {
        return local_answer + " Cloud LLM is not configured.";
    }

    const auto cloud_answer = CloudLlmClient::chat(llm, question);
    if (cloud_answer.empty()) {
        return local_answer + " Cloud LLM returned no answer.";
    }
    return cloud_answer;
}

std::string speechTextForAnswer(const std::string& question,
                                const std::string& display_answer,
                                const Catalog& catalog,
                                const Cart& cart) {
    if (display_answer.find("Milk price is") == 0) {
        return "牛奶的价格是四元五角。";
    }
    if (display_answer.find("Cola price is") == 0) {
        return "可乐的价格是三元五角。";
    }
    if (display_answer.find("Water price is") == 0) {
        return "矿泉水的价格是两元。";
    }
    if (display_answer.find("Bread price is") == 0) {
        return "面包的价格是五元。";
    }
    if (display_answer.find("Available products:") == 0) {
        return "当前商品包括矿泉水，可乐，牛奶，面包，方便面，薯片，饼干，牙膏，纸巾和香皂。";
    }
    if (display_answer.find("Cart is empty") == 0) {
        return "当前购物车为空。";
    }
    if (display_answer.find("Checkout creates") == 0) {
        return "结算时，系统会根据购物车生成订单和支付链接。";
    }
    if (display_answer.find("Voice prompt:") == 0) {
        return "这是一个基于开发板的智能零售演示系统，支持语音问答，商品识别，购物车和结算。";
    }
    return display_answer;
}

void speakIfConfigured(const std::string& text) {
    const auto tts = CloudTtsClient::loadFromEnvironment();
    if (!CloudTtsClient::isConfigured(tts) || text.empty()) {
        return;
    }
    const std::string audio_path = "/tmp/embed_tts_reply.mp3";
    std::cout << "Speaking...\n";
    if (!CloudTtsClient::synthesizeToAudio(tts, text, audio_path)) {
        std::cout << "TTS failed.\n";
        return;
    }
    if (!CloudTtsClient::playAudio(audio_path)) {
        std::cout << "Audio playback failed.\n";
    }
}

void runVoiceAsk(std::istringstream& input, const Catalog& catalog, const Cart& cart) {
    MicrophoneCaptureConfig mic;
    mic.output_wav = "/tmp/embed_voiceask.wav";
    if (!(input >> mic.seconds)) {
        mic.seconds = 3;
    }

    CloudAsrConfig asr = CloudAsrClient::loadFromEnvironment();
    if (!CloudAsrClient::isConfigured(asr)) {
        std::cout << "Cloud ASR is not configured. Set VOLCANO_ASR_URL, VOLCANO_APP_ID, "
                  << "VOLCANO_ACCESS_TOKEN, and VOLCANO_ASR_CLUSTER, or set VOLCANO_ASR_COMMAND.\n";
        return;
    }

    std::cout << "Recording " << mic.seconds << " seconds...\n";
    if (!MicrophoneDriver::recordToFile(mic)) {
        std::cout << "Microphone recording failed.\n";
        return;
    }

    std::cout << "Recognizing speech...\n";
    const auto recognized = CloudAsrClient::recognizeWav(asr, mic.output_wav);
    if (recognized.empty()) {
        std::cout << "Speech recognition failed or returned empty text.\n";
        return;
    }

    std::cout << "Recognized: " << recognized << "\n";
    const auto answer = answerWithLocalThenLlm(recognized, catalog, cart);
    std::cout << answer << "\n";
    speakIfConfigured(speechTextForAnswer(recognized, answer, catalog, cart));
}

bool runVoiceAskOnce(const CloudAsrConfig& asr, const Catalog& catalog, const Cart& cart, int seconds) {
    MicrophoneCaptureConfig mic;
    mic.output_wav = "/tmp/embed_voiceask.wav";
    mic.seconds = seconds;

    std::cout << "Listening...\n";
    if (!MicrophoneDriver::recordToFile(mic)) {
        std::cout << "Microphone recording failed.\n";
        return false;
    }

    const auto recognized = CloudAsrClient::recognizeWav(asr, mic.output_wav);
    if (recognized.empty()) {
        std::cout << "No speech recognized.\n";
        return true;
    }

    std::cout << "Recognized: " << recognized << "\n";
    const auto answer = answerWithLocalThenLlm(recognized, catalog, cart);
    std::cout << answer << "\n";
    speakIfConfigured(speechTextForAnswer(recognized, answer, catalog, cart));
    return true;
}

void runContinuousVoiceAsk(const Catalog& catalog, const Cart& cart) {
    CloudAsrConfig asr = CloudAsrClient::loadFromEnvironment();
    if (!CloudAsrClient::isConfigured(asr)) {
        std::cout << "Cloud ASR is not configured. Run scripts/run_voiceask.sh or set ASR environment variables.\n";
        return;
    }

    std::cout << "Continuous voice mode started. Press Ctrl+C to stop.\n";
    while (true) {
        runVoiceAskOnce(asr, catalog, cart, 2);
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
        } else if (command == "ask") {
            std::string question;
            std::getline(input, question);
            const auto answer = answerWithLocalThenLlm(question, catalog, cart);
            std::cout << answer << "\n";
            speakIfConfigured(speechTextForAnswer(question, answer, catalog, cart));
        } else if (command == "mic") {
            runMicrophoneCapture(input);
        } else if (command == "voiceask") {
            runVoiceAsk(input, catalog, cart);
        } else if (command == "listen") {
            runContinuousVoiceAsk(catalog, cart);
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
