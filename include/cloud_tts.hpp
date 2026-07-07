#pragma once

#include <string>

struct CloudTtsConfig {
    std::string url;
    std::string app_id;
    std::string access_token;
    std::string cluster;
    std::string voice_type;
};

class CloudTtsClient {
public:
    static CloudTtsConfig loadFromEnvironment();
    static bool isConfigured(const CloudTtsConfig& config);
    static std::string buildSynthesizeCommand(const CloudTtsConfig& config,
                                              const std::string& text,
                                              const std::string& response_path);
    static std::string extractAudioData(const std::string& response_json);
    static bool synthesizeToAudio(const CloudTtsConfig& config, const std::string& text, const std::string& audio_path);
    static bool playAudio(const std::string& audio_path);
};
