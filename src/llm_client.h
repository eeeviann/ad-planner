#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>

struct LLMResponse {
    bool success;
    std::string content;
    std::string error;
    int http_code;
};

class LLMClient {
public:
    LLMClient(const std::string& api_url, const std::string& api_key);
    ~LLMClient();


    LLMResponse chat(const std::string& system_prompt,
                     const std::string& user_message,
                     const std::string& model = "deepseek-ai/DeepSeek-V3",
                     int max_tokens = 2048,
                     double temperature = 0.0);

private:
    std::string api_url_;
    std::string api_key_;
};

#endif
