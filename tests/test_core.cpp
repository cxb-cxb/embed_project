#include "cart.hpp"
#include "catalog.hpp"
#include "answer_engine.hpp"
#include "cloud_asr.hpp"
#include "cloud_llm.hpp"
#include "cloud_tts.hpp"
#include "microphone_driver.hpp"
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

    auto byPlainQr = recognizer.recognize({ "", "cola", "" }, catalog);
    expect(byPlainQr.has_value(), "plain qr product id should return a product");
    expect(byPlainQr->product.id == "cola", "plain qr product id should recognize cola");

    auto byQrBarcode = recognizer.recognize({ "", "690100000003", "" }, catalog);
    expect(byQrBarcode.has_value(), "qr barcode payload should return a product");
    expect(byQrBarcode->product.id == "milk", "qr barcode payload should recognize milk");

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

void test_answer_engine() {
    auto catalog = Catalog::loadDefault();
    Cart cart;
    cart.add(*catalog.findById("cola"), 2);

    auto help = AnswerEngine::answer("语音提示", catalog, cart);
    expect(help.find("smart retail") != std::string::npos, "voice prompt should describe smart retail system");
    expect(help.find("camera scan") != std::string::npos, "voice prompt should mention scan capability");

    auto price = AnswerEngine::answer("cola price", catalog, cart);
    expect(price.find("Cola") != std::string::npos, "price answer should include product name");
    expect(price.find("CNY 3.50") != std::string::npos, "price answer should include product price");

    auto cartAnswer = AnswerEngine::answer("当前购物车", catalog, cart);
    expect(cartAnswer.find("Cola x 2") != std::string::npos, "cart answer should include quantities");
    expect(cartAnswer.find("CNY 7.00") != std::string::npos, "cart answer should include total");

    auto products = AnswerEngine::answer("还有什么商品", catalog, cart);
    expect(products.find("Available products") != std::string::npos, "product answer should list products");
    expect(products.find("Milk") != std::string::npos, "product answer should include milk");

    auto fallback = AnswerEngine::answer("tell me a story", catalog, cart);
    expect(AnswerEngine::isFallbackAnswer(fallback), "unhandled question should be marked as fallback");
}

void test_microphone_driver_builds_arecord_command() {
    MicrophoneCaptureConfig config;
    config.device = "hw:0,0";
    config.sample_rate_hz = 48000;
    config.channels = 2;
    config.seconds = 3;
    config.output_wav = "/tmp/question.wav";

    auto command = MicrophoneDriver::buildRecordCommand(config);
    expect(command.find("tinycap /tmp/question.wav.stereo.wav") != std::string::npos,
           "microphone driver should capture stereo source first");
    expect(command.find("-map_channel 0.0.0") != std::string::npos,
           "microphone driver should extract left microphone channel");
    expect(command.find("-ar 16000 -ac 1 /tmp/question.wav") != std::string::npos,
           "microphone driver should create mono 16 kHz wav for ASR");
}

void test_microphone_driver_uses_positive_record_seconds() {
    MicrophoneCaptureConfig config;
    config.seconds = 0;
    config.output_wav = "/tmp/question.wav";

    auto command = MicrophoneDriver::buildRecordCommand(config);
    expect(command.find("-t 2") != std::string::npos,
           "microphone driver should use a safe default chunk duration for continuous listening");
}

void test_cloud_asr_builds_https_command() {
    CloudAsrConfig config;
    config.url = "https://example.test/asr";
    config.app_id = "app";
    config.access_token = "token";
    config.cluster = "cluster";
    config.resource_id = "resource";
    config.timeout_seconds = 9;

    auto command = CloudAsrClient::buildRecognizeCommand(config, "/tmp/question.wav", "/tmp/asr.json");
    expect(command.find("openssl s_client") != std::string::npos, "asr command should use openssl for https");
    expect(command.find("example.test:443") != std::string::npos, "asr command should connect to https host");
    expect(command.find("POST /asr HTTP/1.1") != std::string::npos, "asr command should use url path");
    expect(command.find("base64") != std::string::npos, "asr command should encode wav file");
    expect(command.find("/tmp/question.wav") != std::string::npos, "asr command should include wav path");
    expect(command.find("/tmp/asr.json.request.json") != std::string::npos,
           "asr command should generate json request file");
    expect(command.find("X-Api-Access-Key: token") != std::string::npos,
           "asr command should include access token header");
    expect(command.find("X-Api-App-Key: app") != std::string::npos,
           "asr command should include app id header");
    expect(command.find("X-Api-Resource-Id: resource") != std::string::npos,
           "asr command should include resource id header");
    expect(command.find("X-Api-Sequence: -1") != std::string::npos,
           "asr command should include final sequence header");
}

void test_cloud_asr_extracts_recognized_text() {
    auto text = CloudAsrClient::extractRecognizedText("{\"result\":{\"text\":\"ask cola price\"}}");
    expect(text == "ask cola price", "asr client should extract nested text field");

    auto alt = CloudAsrClient::extractRecognizedText("{\"text\":\"语音提示\"}");
    expect(alt == "语音提示", "asr client should extract top-level text field");

    auto silence = CloudAsrClient::extractRecognizedText(
        "{\"audio_info\":{\"duration\":999},\"result\":{\"additions\":{\"duration\":\"999\"},\"text\":\"\"}}");
    expect(silence.empty(), "asr client should return empty text for silence response");
}

void test_cloud_llm_builds_openai_compatible_command() {
    CloudLlmConfig config;
    config.base_url = "https://ark.cn-beijing.volces.com/api/v3";
    config.api_key = "ark-test";
    config.model = "doubao-seed-1-6-flash-250615";

    auto command = CloudLlmClient::buildChatCommand(config, "tell a story", "/tmp/llm.json");
    expect(command.find("openssl s_client") != std::string::npos, "llm command should use openssl for https");
    expect(command.find("ark.cn-beijing.volces.com:443") != std::string::npos,
           "llm command should connect to ark host");
    expect(command.find("POST /api/v3/chat/completions HTTP/1.1") != std::string::npos,
           "llm command should call chat completions path");
    expect(command.find("Authorization: Bearer ark-test") != std::string::npos,
           "llm command should include API key");
    expect(command.find("doubao-seed-1-6-flash-250615") != std::string::npos,
           "llm command should include model");
    expect(command.find("Simplified Chinese") != std::string::npos,
           "llm system prompt should require Chinese replies");
}

void test_cloud_llm_extracts_assistant_text() {
    auto text = CloudLlmClient::extractAssistantText(
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello there.\"}}]}");
    expect(text == "Hello there.", "llm client should extract assistant content");
}

void test_cloud_tts_builds_request_command() {
    CloudTtsConfig config;
    config.url = "https://openspeech.bytedance.com/api/v3/tts/unidirectional";
    config.app_id = "app";
    config.access_token = "token";
    config.cluster = "cluster";
    config.voice_type = "voice";

    auto command = CloudTtsClient::buildSynthesizeCommand(config, "hello", "/tmp/tts.json");
    expect(command.find("openssl s_client") != std::string::npos, "tts command should use openssl");
    expect(command.find("POST /api/v3/tts/unidirectional HTTP/1.1") != std::string::npos,
           "tts command should call tts v3 path");
    expect(command.find("X-Api-Access-Key: token") != std::string::npos, "tts command should include token");
    expect(command.find("X-Api-Resource-Id: cluster") != std::string::npos, "tts command should include resource id");
    expect(command.find("\"speaker\":\"voice\"") != std::string::npos, "tts command should include speaker");
}

void test_cloud_tts_extracts_audio_data() {
    auto data = CloudTtsClient::extractAudioData("{\"data\":\"YWI=\"}\n{\"data\":\"Yw==\"}");
    expect(data == "YWI=Yw==", "tts client should combine base64 audio chunks");
}

}  // namespace

int main() {
    try {
        test_catalog();
        test_recognition();
        test_cart_and_payment();
        test_voice();
        test_answer_engine();
        test_microphone_driver_builds_arecord_command();
        test_microphone_driver_uses_positive_record_seconds();
        test_cloud_asr_builds_https_command();
        test_cloud_asr_extracts_recognized_text();
        test_cloud_llm_builds_openai_compatible_command();
        test_cloud_llm_extracts_assistant_text();
        test_cloud_tts_builds_request_command();
        test_cloud_tts_extracts_audio_data();
    } catch (const std::exception& ex) {
        std::cerr << "TEST FAILED: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "All retail core tests passed.\n";
    return EXIT_SUCCESS;
}
