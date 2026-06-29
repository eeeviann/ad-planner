#include "llm_client.h"
#include <cstdlib>
#include <fstream>

LLMClient::LLMClient(const std::string& api_url, const std::string& api_key)
    : api_url_(api_url), api_key_(api_key) {}

LLMClient::~LLMClient() {}

static std::string base64_encode(const std::string& input) {
    static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    int in_len = (int)input.size();
    int in_ = 0;

    while (in_len--) {
        char_array_3[i++] = input[in_++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; i < 4; i++) result += b64_table[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) result += b64_table[char_array_4[j]];
        while((i++ < 3)) result += '=';
    }
    return result;
}

LLMResponse LLMClient::chat(
    const std::string& system_prompt,
    const std::string& user_message,
    const std::string& model,
    int max_tokens,
    double temperature
) {
    (void)model; (void)max_tokens; (void)temperature;

    std::string sys_b64 = base64_encode(system_prompt);
    std::string user_b64 = base64_encode(user_message);

    std::string cmd = "python3 solver/llm_call.py \"" + api_url_ + "\" \"" + api_key_ + "\" " + sys_b64 + " " + user_b64;

    char buffer[4096];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {false, "", "popen failed", 0};

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        return {false, result, "Script exited with code " + std::to_string(status), 0};
    }
    return {true, result, "", 200};
}
