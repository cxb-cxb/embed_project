#include "cloud_llm.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (const char c : value) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += c;
                break;
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

CloudLlmConfig CloudLlmClient::loadFromEnvironment() {
    CloudLlmConfig config;
    config.base_url = envValue("ARK_BASE_URL");
    if (config.base_url.empty()) {
        config.base_url = "https://ark.cn-beijing.volces.com/api/v3";
    }
    config.api_key = envValue("ARK_API_KEY");
    config.model = envValue("ARK_MODEL");
    if (config.model.empty()) {
        config.model = "doubao-seed-1-6-flash-250615";
    }
    return config;
}

bool CloudLlmClient::isConfigured(const CloudLlmConfig& config) {
    return !config.base_url.empty() && !config.api_key.empty() && !config.model.empty();
}

std::string CloudLlmClient::buildChatCommand(const CloudLlmConfig& config,
                                             const std::string& user_text,
                                             const std::string& response_path) {
    const std::string url = config.base_url + "/chat/completions";
    const auto host = httpsHost(url);
    const auto path = httpsPath(url);
    const std::string request_path = response_path + ".request.json";
    const std::string system_prompt =
        "You are a concise smart retail voice assistant for a student embedded board demo. "
        "Answer in short English ASCII text only. Avoid Chinese characters to prevent terminal mojibake. "
        "If asked about available products, mention common demo products such as milk, cola, bread, water, chips, coffee, tea, cookies, yogurt, and instant noodles.";
    const std::string request_json =
        "{\"model\":\"" + jsonEscape(config.model) +
        "\",\"messages\":[{\"role\":\"system\",\"content\":\"" + jsonEscape(system_prompt) +
        "\"},{\"role\":\"user\",\"content\":\"" + jsonEscape(user_text) +
        "\"}],\"temperature\":0.4,\"max_tokens\":160}";

    std::ostringstream command;
    command << "sh -c " << shellQuote(
        "printf " + shellQuote(request_json) + " > " + shellQuote(request_path) +
        "; body_len=$(wc -c < " + shellQuote(request_path) + ")" +
        "; { printf " + shellQuote("POST " + path + " HTTP/1.1\r\n") +
        "; printf " + shellQuote("Host: " + host + "\r\n") +
        "; printf " + shellQuote("Connection: close\r\n") +
        "; printf " + shellQuote("Authorization: Bearer " + config.api_key + "\r\n") +
        "; printf " + shellQuote("Content-Type: application/json\r\n") +
        "; printf 'Content-Length: %s\\r\\n' \"$body_len\"" +
        "; printf '\\r\\n'" +
        "; cat " + shellQuote(request_path) +
        "; } | openssl s_client -quiet -connect " + shellQuote(host + ":443") +
        " -servername " + shellQuote(host) + " > " + shellQuote(response_path) +
        " 2>/tmp/embed_llm_ssl.log");
    return command.str();
}

std::string CloudLlmClient::extractAssistantText(const std::string& response_json) {
    return extractJsonStringAfter(response_json, "\"content\"");
}

std::string CloudLlmClient::chat(const CloudLlmConfig& config, const std::string& user_text) {
    const std::string response_path = "/tmp/embed_llm_response.json";
    const std::string command = buildChatCommand(config, user_text, response_path);
    if (std::system(command.c_str()) != 0) {
        return "";
    }
    return extractAssistantText(readTextFile(response_path));
}
