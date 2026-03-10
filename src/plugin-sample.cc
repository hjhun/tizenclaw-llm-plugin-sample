#include <curl/curl.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "tizenclaw_llm_backend.h"

using json = nlohmann::json;

extern "C" {

struct OpenAIBackend {
  std::string name;
  std::string api_key;
  std::string model;
  std::string endpoint;
};

static OpenAIBackend* g_backend = nullptr;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                            void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

bool TIZENCLAW_LLM_BACKEND_INITIALIZE(const char* config_json_str) {
  if (g_backend) return true;

  g_backend = new (std::nothrow) OpenAIBackend();
  if (!g_backend) return false;

  g_backend->name = "plugin-sample";
  g_backend->model = "gpt-3.5-turbo";
  g_backend->endpoint = "https://api.openai.com/v1/chat/completions";

  if (config_json_str) {
    try {
      auto config = json::parse(config_json_str);
      if (config.contains("name")) g_backend->name = config["name"];
      if (config.contains("api_key")) g_backend->api_key = config["api_key"];
      if (config.contains("model")) g_backend->model = config["model"];
      if (config.contains("endpoint")) g_backend->endpoint = config["endpoint"];
    } catch (...) {
      std::cerr << "[Plugin Sample] Failed to parse config JSON" << std::endl;
    }
  }

  if (g_backend->api_key.empty()) {
    const char* env_key = getenv("OPENAI_API_KEY");
    if (env_key) g_backend->api_key = env_key;
  }
  std::cout << "[Plugin Sample] Initialized (model: " << g_backend->model << ", endpoint: " << g_backend->endpoint << ")"
            << std::endl;
  return true;
}

const char* TIZENCLAW_LLM_BACKEND_GET_NAME(void) {
  return g_backend ? g_backend->name.c_str() : "unknown";
}

void TIZENCLAW_LLM_BACKEND_SHUTDOWN(void) {
  if (g_backend) {
    std::cout << "[Plugin Sample] Shutdown" << std::endl;
    delete g_backend;
    g_backend = nullptr;
  }
}

tizenclaw_llm_response_h TIZENCLAW_LLM_BACKEND_CHAT(
    tizenclaw_llm_messages_h messages_arr, tizenclaw_llm_tools_h tools_arr,
    tizenclaw_llm_backend_chunk_cb on_chunk, void* user_data,
    const char* system_prompt) {
  tizenclaw_llm_response_h response = nullptr;
  tizenclaw_llm_response_create(&response);

  if (!g_backend) {
    tizenclaw_llm_response_set_error_message(response,
                                             "Plugin not initialized");
    return response;
  }

  json payload;
  payload["model"] = g_backend->model;

  json req_messages = json::array();
  if (system_prompt && strlen(system_prompt) > 0) {
    req_messages.push_back({{"role", "system"}, {"content", system_prompt}});
  }

  if (messages_arr) {
    auto msg_cb = [](tizenclaw_llm_message_h msg, void* ud) -> bool {
      auto* req_msgs = static_cast<json*>(ud);
      json m;
      char* role = nullptr;
      tizenclaw_llm_message_get_role(msg, &role);
      if (role) {
        m["role"] = role;
        free(role);
      }

      char* text = nullptr;
      tizenclaw_llm_message_get_text(msg, &text);
      if (text) {
        m["content"] = text;
        free(text);
      }

      // Tool calls
      json tool_calls = json::array();
      auto tc_cb = [](tizenclaw_llm_tool_call_h tc, void* u) -> bool {
        auto* tcs = static_cast<json*>(u);
        json c;
        char* id = nullptr;
        tizenclaw_llm_tool_call_get_id(tc, &id);
        if (id) {
          c["id"] = id;
          free(id);
        }

        char* name = nullptr;
        tizenclaw_llm_tool_call_get_name(tc, &name);
        if (name) {
          c["type"] = "function";
          c["function"]["name"] = name;
          free(name);
        }

        char* args = nullptr;
        tizenclaw_llm_tool_call_get_args_json(tc, &args);
        if (args) {
          c["function"]["arguments"] = args;
          free(args);
        }

        tcs->push_back(c);
        return true;
      };
      tizenclaw_llm_message_foreach_tool_calls(msg, tc_cb, &tool_calls);
      if (!tool_calls.empty()) m["tool_calls"] = tool_calls;

      // Handle tool results
      char* tool_name = nullptr;
      tizenclaw_llm_message_get_tool_name(msg, &tool_name);
      if (tool_name) {
        m["name"] = tool_name;
        free(tool_name);

        char* tc_id = nullptr;
        tizenclaw_llm_message_get_tool_call_id(msg, &tc_id);
        if (tc_id) {
          m["tool_call_id"] = tc_id;
          free(tc_id);
        }

        char* res = nullptr;
        tizenclaw_llm_message_get_tool_result_json(msg, &res);
        if (res) {
          m["content"] = res;
          free(res);
        }  // OpenAI expects tool result in content
      }

      req_msgs->push_back(m);
      return true;
    };
    tizenclaw_llm_messages_foreach(messages_arr, msg_cb, &req_messages);
  }
  payload["messages"] = req_messages;

  if (tools_arr) {
    json req_tools = json::array();
    auto tool_cb = [](tizenclaw_llm_tool_h tool, void* ud) -> bool {
      auto* rt = static_cast<json*>(ud);
      json t;
      t["type"] = "function";
      json func;

      char* name = nullptr;
      tizenclaw_llm_tool_get_name(tool, &name);
      if (name) {
        func["name"] = name;
        free(name);
      }

      char* desc = nullptr;
      tizenclaw_llm_tool_get_description(tool, &desc);
      if (desc) {
        func["description"] = desc;
        free(desc);
      }

      char* params = nullptr;
      tizenclaw_llm_tool_get_parameters_json(tool, &params);
      if (params) {
        try {
          func["parameters"] = json::parse(params);
        } catch (...) {
        }
        free(params);
      }

      t["function"] = func;
      rt->push_back(t);
      return true;
    };
    tizenclaw_llm_tools_foreach(tools_arr, tool_cb, &req_tools);
    if (!req_tools.empty()) payload["tools"] = req_tools;
  }

  std::string payload_str = payload.dump();

  CURL* curl = curl_easy_init();
  if (curl) {
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + g_backend->api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL,
                     g_backend->endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      long http_code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
      tizenclaw_llm_response_set_http_status(response, http_code);

      try {
        auto resp_json = json::parse(readBuffer);
        if (resp_json.contains("choices") && resp_json["choices"].size() > 0) {
          auto message = resp_json["choices"][0]["message"];
          if (message.contains("content") && !message["content"].is_null()) {
            tizenclaw_llm_response_set_text(
                response, message["content"].get<std::string>().c_str());
          }
          if (message.contains("tool_calls")) {
            for (const auto& tc_json : message["tool_calls"]) {
              tizenclaw_llm_tool_call_h tc_h = nullptr;
              tizenclaw_llm_tool_call_create(&tc_h);
              if (tc_json.contains("id")) {
                tizenclaw_llm_tool_call_set_id(
                    tc_h, tc_json["id"].get<std::string>().c_str());
              }
              if (tc_json.contains("function")) {
                auto fn = tc_json["function"];
                if (fn.contains("name"))
                  tizenclaw_llm_tool_call_set_name(
                      tc_h, fn["name"].get<std::string>().c_str());
                if (fn.contains("arguments"))
                  tizenclaw_llm_tool_call_set_args_json(
                      tc_h, fn["arguments"].get<std::string>().c_str());
              }
              tizenclaw_llm_response_add_llm_tool_call(response, tc_h);
              tizenclaw_llm_tool_call_destroy(tc_h);
            }
          }
          tizenclaw_llm_response_set_success(response, true);
        } else if (resp_json.contains("error")) {
          tizenclaw_llm_response_set_error_message(
              response, resp_json["error"].dump().c_str());
        }

        if (resp_json.contains("usage")) {
          auto usage = resp_json["usage"];
          if (usage.contains("prompt_tokens"))
            tizenclaw_llm_response_set_prompt_tokens(
                response, usage["prompt_tokens"].get<int>());
          if (usage.contains("completion_tokens"))
            tizenclaw_llm_response_set_completion_tokens(
                response, usage["completion_tokens"].get<int>());
          if (usage.contains("total_tokens"))
            tizenclaw_llm_response_set_total_tokens(
                response, usage["total_tokens"].get<int>());
        }

      } catch (...) {
        tizenclaw_llm_response_set_error_message(
            response, "Failed to parse OpenAI response");
      }
    } else {
      std::string err =
          std::string("CURL request failed: ") + curl_easy_strerror(res);
      tizenclaw_llm_response_set_error_message(response, err.c_str());
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }

  return response;
}

}  // extern "C"
