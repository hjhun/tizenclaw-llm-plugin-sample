#include "tizenclaw_llm_backend.h"
#include <iostream>
#include <string>
#include <curl/curl.h>
#include <json.hpp>

using json = nlohmann::json;

extern "C" {

struct OpenAIBackend {
    std::string name;
    std::string api_key;
    std::string model;
};

struct OpenAIResponse {
    int success;
    std::string text;
    std::string tools_json;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

tizenclaw_llm_backend_h TIZENCLAW_LLM_BACKEND_INITIALIZE(const char* config_json_str) {
    auto* backend = new OpenAIBackend();
    backend->name = "openai_plugin";
    backend->model = "gpt-4o";
    
    if (config_json_str) {
        try {
            auto config = json::parse(config_json_str);
            if (config.contains("api_key")) backend->api_key = config["api_key"];
            if (config.contains("model")) backend->model = config["model"];
        } catch (...) {
            std::cerr << "[OpenAI Plugin] Failed to parse config JSON" << std::endl;
        }
    }
    
    if (backend->api_key.empty()) {
        const char* env_key = getenv("OPENAI_API_KEY");
        if (env_key) backend->api_key = env_key;
    }
    std::cout << "[OpenAI Plugin] Initialized (model: " << backend->model << ")" << std::endl;
    return (tizenclaw_llm_backend_h)backend;
}

const char* TIZENCLAW_LLM_BACKEND_GET_NAME(tizenclaw_llm_backend_h handle) {
    if (!handle) return "unknown";
    return ((OpenAIBackend*)handle)->name.c_str();
}

tizenclaw_llm_response_h TIZENCLAW_LLM_BACKEND_CHAT(
    tizenclaw_llm_backend_h handle,
    const char* messages_json,
    const char* tools_json,
    const char* system_prompt) {
    
    auto* backend = (OpenAIBackend*)handle;
    auto* response = new OpenAIResponse();
    response->success = 0;
    response->text = "";
    response->tools_json = "[]";

    json payload;
    payload["model"] = backend->model;
    
    json req_messages = json::array();
    if (system_prompt && strlen(system_prompt) > 0) {
        req_messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }

    if (messages_json) {
        try {
            auto msgs = json::parse(messages_json);
            for (const auto& m : msgs) {
                json msg;
                if (m.contains("role")) msg["role"] = m["role"];
                if (m.contains("text")) msg["content"] = m["text"];
                // Basic tool call formatting if present
                if (m.contains("tool_calls")) msg["tool_calls"] = m["tool_calls"];
                if (m.contains("tool_call_id")) msg["tool_call_id"] = m["tool_call_id"];
                if (m.contains("name")) msg["name"] = m["name"];
                req_messages.push_back(msg);
            }
        } catch (...) {}
    }
    payload["messages"] = req_messages;

    if (tools_json) {
        try {
            auto tools = json::parse(tools_json);
            if (tools.is_array() && !tools.empty()) {
                payload["tools"] = tools;
            }
        } catch (...) {}
    }

    std::string payload_str = payload.dump();

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header = "Authorization: Bearer " + backend->api_key;
        headers = curl_slist_append(headers, auth_header.c_str());

        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        
        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            try {
                auto resp_json = json::parse(readBuffer);
                if (resp_json.contains("choices") && resp_json["choices"].size() > 0) {
                    auto message = resp_json["choices"][0]["message"];
                    if (message.contains("content") && !message["content"].is_null()) {
                        response->text = message["content"].get<std::string>();
                    }
                    if (message.contains("tool_calls")) {
                        response->tools_json = message["tool_calls"].dump();
                    }
                    response->success = 1;
                } else if (resp_json.contains("error")) {
                    response->text = "OpenAI API Error: " + resp_json["error"]["message"].get<std::string>();
                }
            } catch (...) {
                response->text = "Failed to parse OpenAI response: " + readBuffer;
            }
        } else {
            response->text = std::string("CURL request failed: ") + curl_easy_strerror(res);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return (tizenclaw_llm_response_h)response;
}

const char* TIZENCLAW_LLM_RESPONSE_GET_TEXT(tizenclaw_llm_response_h handle) {
    if (!handle) return NULL;
    return ((OpenAIResponse*)handle)->text.c_str();
}

const char* TIZENCLAW_LLM_RESPONSE_GET_TOOL_CALLS_JSON(tizenclaw_llm_response_h handle) {
    if (!handle) return NULL;
    return ((OpenAIResponse*)handle)->tools_json.c_str();
}

int TIZENCLAW_LLM_RESPONSE_IS_SUCCESS(tizenclaw_llm_response_h handle) {
    if (!handle) return 0;
    return ((OpenAIResponse*)handle)->success;
}

void TIZENCLAW_LLM_RESPONSE_DESTROY(tizenclaw_llm_response_h handle) {
    if (handle) {
        delete (OpenAIResponse*)handle;
    }
}

void TIZENCLAW_LLM_BACKEND_SHUTDOWN(tizenclaw_llm_backend_h handle) {
    if (handle) {
        std::cout << "[OpenAI Plugin] Shutdown" << std::endl;
        delete (OpenAIBackend*)handle;
    }
}

} // extern "C"
