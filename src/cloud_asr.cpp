#include "cloud_asr.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
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

void replaceAll(std::string& value, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string httpsHost(const std::string& url) {
    const std::string prefix = "https://";
    if (!startsWith(url, prefix)) {
        return "";
    }
    const auto host_start = prefix.size();
    const auto path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        return url.substr(host_start);
    }
    return url.substr(host_start, path_start - host_start);
}

std::string httpsPath(const std::string& url) {
    const std::string prefix = "https://";
    if (!startsWith(url, prefix)) {
        return "/";
    }
    const auto path_start = url.find('/', prefix.size());
    if (path_start == std::string::npos) {
        return "/";
    }
    return url.substr(path_start);
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
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
            if (c == 'n') {
                value += '\n';
            } else if (c == 't') {
                value += '\t';
            } else {
                value += c;
            }
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

CloudAsrConfig CloudAsrClient::loadFromEnvironment() {
    CloudAsrConfig config;
    config.url = envValue("VOLCANO_ASR_URL");
    if (config.url.empty()) {
        config.url = "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash";
    }
    config.app_id = envValue("VOLCANO_APP_ID");
    config.access_token = envValue("VOLCANO_ACCESS_TOKEN");
    config.cluster = envValue("VOLCANO_ASR_CLUSTER");
    config.resource_id = envValue("VOLCANO_ASR_RESOURCE_ID");
    if (config.resource_id.empty()) {
        config.resource_id = config.cluster;
    }
    config.command_template = envValue("VOLCANO_ASR_COMMAND");
    return config;
}

bool CloudAsrClient::isConfigured(const CloudAsrConfig& config) {
    if (!config.command_template.empty()) {
        return true;
    }
    return !config.url.empty() && !config.app_id.empty() && !config.access_token.empty() &&
           !config.resource_id.empty();
}

std::string CloudAsrClient::buildRecognizeCommand(const CloudAsrConfig& config,
                                                  const std::string& wav_path,
                                                  const std::string& response_path) {
    if (!config.command_template.empty()) {
        std::string command = config.command_template;
        replaceAll(command, "{wav}", shellQuote(wav_path));
        replaceAll(command, "{out}", shellQuote(response_path));
        return command;
    }

    const std::string request_path = response_path + ".request.json";
    const auto host = httpsHost(config.url);
    const auto path = httpsPath(config.url);
    std::ostringstream command;
    command << "sh -c " << shellQuote(
        "printf '{\"user\":{\"uid\":\"qsm368\"},\"audio\":{\"data\":\"' > " + shellQuote(request_path) +
        "; base64 " + shellQuote(wav_path) + " | tr -d '\\n' >> " + shellQuote(request_path) +
        "; printf '\"},\"request\":{\"model_name\":\"bigmodel\",\"enable_itn\":true}}' >> " +
        shellQuote(request_path) +
        "; body_len=$(wc -c < " + shellQuote(request_path) + ")" +
        "; { printf " + shellQuote("POST " + path + " HTTP/1.1\r\n") +
        "; printf " + shellQuote("Host: " + host + "\r\n") +
        "; printf " + shellQuote("Connection: close\r\n") +
        "; printf " + shellQuote("X-Api-App-Key: " + config.app_id + "\r\n") +
        "; printf " + shellQuote("X-Api-Access-Key: " + config.access_token + "\r\n") +
        "; printf " + shellQuote("X-Api-Resource-Id: " + config.resource_id + "\r\n") +
        "; printf " + shellQuote("X-Api-Request-Id: qsm368-voiceask\r\n") +
        "; printf " + shellQuote("X-Api-Sequence: -1\r\n") +
        "; printf " + shellQuote("Content-Type: application/json\r\n") +
        "; printf 'Content-Length: %s\\r\\n' \"$body_len\"" +
        "; printf '\\r\\n'" +
        "; cat " + shellQuote(request_path) +
        "; } | openssl s_client -quiet -connect " + shellQuote(host + ":443") +
        " -servername " + shellQuote(host) + " > " + shellQuote(response_path) + " 2>/tmp/embed_asr_ssl.log");
    return command.str();
}

std::string CloudAsrClient::extractRecognizedText(const std::string& response_json) {
    std::string text = extractJsonStringAfter(response_json, "\"text\"");
    if (!text.empty()) {
        return text;
    }
    text = extractJsonStringAfter(response_json, "\"utterance\"");
    if (!text.empty()) {
        return text;
    }
    return "";
}

std::string CloudAsrClient::recognizeWav(const CloudAsrConfig& config, const std::string& wav_path) {
    const std::string response_path = "/tmp/embed_asr_response.json";
    const std::string command = buildRecognizeCommand(config, wav_path, response_path);
    if (std::system(command.c_str()) != 0) {
        return "";
    }
    return extractRecognizedText(readTextFile(response_path));
}
