#pragma once

#include <string>

struct CloudAsrConfig {
    std::string url;
    std::string app_id;
    std::string access_token;
    std::string cluster;
    std::string resource_id;
    std::string command_template;
    int timeout_seconds = 20;
};

class CloudAsrClient {
public:
    static CloudAsrConfig loadFromEnvironment();
    static bool isConfigured(const CloudAsrConfig& config);
    static std::string buildRecognizeCommand(const CloudAsrConfig& config,
                                             const std::string& wav_path,
                                             const std::string& response_path);
    static std::string extractRecognizedText(const std::string& response_json);
    static std::string recognizeWav(const CloudAsrConfig& config, const std::string& wav_path);
};
