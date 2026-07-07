#pragma once

#include <string>

struct MicrophoneCaptureConfig {
    std::string device = "hw:0,0";
    int sample_rate_hz = 48000;
    int channels = 2;
    int seconds = 3;
    std::string output_wav = "/tmp/embed_question.wav";
};

class MicrophoneDriver {
public:
    static std::string buildRecordCommand(const MicrophoneCaptureConfig& config);
    static bool recordToFile(const MicrophoneCaptureConfig& config);
};
