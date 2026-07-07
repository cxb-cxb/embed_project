#include "cloud_tts.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string httpsHost(const std::string& url) {
    const std::string prefix = "https://";
    if (!startsWith(url, prefix)) {
        return "";
    }
    const auto host_start = prefix.size();
    const auto path_start = url.find('/', host_start);
    return path_start == std::string::npos ? url.substr(host_start) : url.substr(host_start, path_start - host_start);
}

std::string httpsPath(const std::string& url) {
    const std::string prefix = "https://";
    if (!startsWith(url, prefix)) {
        return "/";
    }
    const auto path_start = url.find('/', prefix.size());
    return path_start == std::string::npos ? "/" : url.substr(path_start);
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (const char c : value) {
        if (c == '\\') {
            escaped += "\\\\";
        } else if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\n') {
            escaped += "\\n";
        } else if (c != '\r') {
            escaped += c;
        }
    }
    return escaped;
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

void writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

std::string extractJsonStringAfter(const std::string& json, const std::string& marker) {
    const auto key_pos = json.find(marker);
    if (key_pos == std::string::npos) {
        return "";
    }
    auto pos = json.find(':', key_pos + marker.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find('"', pos);
    if (pos == std::string::npos) {
        return "";
    }
    ++pos;

    std::string value;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escaped) {
            value += c;
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        value += c;
    }
    return value;
}

}  // namespace

CloudTtsConfig CloudTtsClient::loadFromEnvironment() {
    CloudTtsConfig config;
    config.url = envValue("VOLCANO_TTS_URL");
    if (config.url.empty()) {
        config.url = "https://openspeech.bytedance.com/api/v3/tts/unidirectional";
    }
    config.app_id = envValue("VOLCANO_APP_ID");
    config.access_token = envValue("VOLCANO_ACCESS_TOKEN");
    config.cluster = envValue("VOLCANO_TTS_CLUSTER");
    config.voice_type = envValue("VOLCANO_TTS_VOICE_TYPE");
    return config;
}

bool CloudTtsClient::isConfigured(const CloudTtsConfig& config) {
    return !config.url.empty() && !config.app_id.empty() && !config.access_token.empty() &&
           !config.cluster.empty() && !config.voice_type.empty();
}

std::string CloudTtsClient::buildSynthesizeCommand(const CloudTtsConfig& config,
                                                   const std::string& text,
                                                   const std::string& response_path) {
    const auto host = httpsHost(config.url);
    const auto path = httpsPath(config.url);
    const std::string request_path = response_path + ".request.json";
    const std::string request_json =
        "{\"req_params\":{\"text\":\"" + jsonEscape(text) + "\",\"speaker\":\"" +
        jsonEscape(config.voice_type) +
        "\",\"audio_params\":{\"format\":\"mp3\",\"sample_rate\":48000},\"additions\":{\"disable_markdown_filter\":true}}}";

    std::ostringstream command;
    command << "sh -c " << shellQuote(
        "printf " + shellQuote(request_json) + " > " + shellQuote(request_path) +
        "; body_len=$(wc -c < " + shellQuote(request_path) + ")" +
        "; { printf " + shellQuote("POST " + path + " HTTP/1.1\r\n") +
        "; printf " + shellQuote("Host: " + host + "\r\n") +
        "; printf " + shellQuote("Connection: close\r\n") +
        "; printf " + shellQuote("X-Api-App-Id: " + config.app_id + "\r\n") +
        "; printf " + shellQuote("X-Api-Access-Key: " + config.access_token + "\r\n") +
        "; printf " + shellQuote("X-Api-Resource-Id: " + config.cluster + "\r\n") +
        "; printf " + shellQuote("X-Api-Request-Id: qsm368-tts\r\n") +
        "; printf " + shellQuote("Content-Type: application/json\r\n") +
        "; printf 'Content-Length: %s\\r\\n' \"$body_len\"" +
        "; printf '\\r\\n'" +
        "; cat " + shellQuote(request_path) +
        "; } | openssl s_client -quiet -connect " + shellQuote(host + ":443") +
        " -servername " + shellQuote(host) + " > " + shellQuote(response_path) +
        " 2>/tmp/embed_tts_ssl.log");
    return command.str();
}

std::string CloudTtsClient::extractAudioData(const std::string& response_json) {
    std::string combined;
    std::size_t pos = 0;
    while (pos < response_json.size()) {
        const auto found = response_json.find("\"data\"", pos);
        if (found == std::string::npos) {
            break;
        }
        const auto data = extractJsonStringAfter(response_json.substr(found), "\"data\"");
        if (!data.empty()) {
            combined += data;
        }
        pos = found + 6;
    }
    return combined;
}

bool CloudTtsClient::synthesizeToAudio(const CloudTtsConfig& config,
                                       const std::string& text,
                                       const std::string& audio_path) {
    const std::string response_path = "/tmp/embed_tts_response.json";
    if (std::system(buildSynthesizeCommand(config, text, response_path).c_str()) != 0) {
        return false;
    }
    const auto data = extractAudioData(readTextFile(response_path));
    if (data.empty()) {
        return false;
    }
    const std::string b64_path = "/tmp/embed_tts_audio.b64";
    writeTextFile(b64_path, data);
    const std::string decode = "base64 -d " + shellQuote(b64_path) + " > " + shellQuote(audio_path);
    return std::system(decode.c_str()) == 0;
}

bool CloudTtsClient::playAudio(const std::string& audio_path) {
    std::system("amixer -c 0 cset numid=1 2 >/dev/null 2>&1");
    std::system("amixer -c 0 cset numid=5 220,220 >/dev/null 2>&1");
    return std::system(("mpg123 -q " + shellQuote(audio_path) + " >/tmp/embed_tts_play.log 2>&1").c_str()) == 0;
}
