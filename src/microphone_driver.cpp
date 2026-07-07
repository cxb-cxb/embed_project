#include "microphone_driver.hpp"

#include <cstdlib>
#include <sstream>

std::string MicrophoneDriver::buildRecordCommand(const MicrophoneCaptureConfig& config) {
    const int seconds = config.seconds > 0 ? config.seconds : 2;
    const std::string stereo_path = config.output_wav + ".stereo.wav";
    std::ostringstream command;
    command << "sh -c '"
            << "tinycap " << stereo_path
            << " -D 0"
            << " -d 0"
            << " -c " << config.channels
            << " -r " << config.sample_rate_hz
            << " -b 16"
            << " -t " << seconds
            << " && ffmpeg -y -i " << stereo_path
            << " -map_channel 0.0.0 -ar 16000 -ac 1 " << config.output_wav
            << " >/tmp/embed_mic_ffmpeg.log 2>&1"
            << "'";
    return command.str();
}

bool MicrophoneDriver::recordToFile(const MicrophoneCaptureConfig& config) {
    return std::system(buildRecordCommand(config).c_str()) == 0;
}
