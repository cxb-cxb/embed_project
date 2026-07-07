#pragma once

#include <string>

struct CloudLlmConfig {
    std::string base_url;
    std::string api_key;
    std::string model;
    int timeout_seconds = 30;
};

class CloudLlmClient {
public:
    static CloudLlmConfig loadFromEnvironment();
    static bool isConfigured(const CloudLlmConfig& config);
    static std::string buildChatCommand(const CloudLlmConfig& config,
                                        const std::string& user_text,
                                        const std::string& response_path);
    static std::string extractAssistantText(const std::string& response_json);
    static std::string chat(const CloudLlmConfig& config, const std::string& user_text);
};
