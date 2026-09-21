#include "llm_client.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "shell_command.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {

// 修复前：API Key 作为命令行参数传给 python 脚本，
// 任何能看进程列表的人都能读到它；Key 里若含引号还会破坏命令。
// 现在改为通过环境变量传递，命令行上不再出现密钥。
const char* kKeyEnvVar = "ADPLANNER_LLM_API_KEY";

void set_env_var(const std::string& key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

std::string default_python() {
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

std::string base64_encode(const std::string& input) {
    static const char* b64_table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = input.size();
    size_t in_ = 0;

    while (in_len--) {
        char_array_3[i++] = static_cast<unsigned char>(input[in_++]);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) result += b64_table[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) result += b64_table[char_array_4[j]];
        while (i++ < 3) result += '=';
    }
    return result;
}

}  // namespace

LLMClient::LLMClient(const std::string& api_url,
                     const std::string& api_key,
                     const std::string& python_path,
                     const std::string& script_path)
    : api_url_(api_url),
      api_key_(api_key),
      python_path_(python_path.empty() ? default_python() : python_path),
      script_path_(script_path) {}

LLMClient::~LLMClient() {}

LLMResponse LLMClient::chat(const std::string& system_prompt,
                            const std::string& user_message,
                            const std::string& model,
                            int max_tokens,
                            double temperature) {
    (void)model;
    (void)max_tokens;
    (void)temperature;

    const std::string sys_b64 = base64_encode(system_prompt);
    const std::string user_b64 = base64_encode(user_message);

    set_env_var(kKeyEnvVar, api_key_);

    const std::string cmd = for_shell(
        shell_quote(python_path_) + " " + shell_quote(script_path_) + " " +
        shell_quote(api_url_) + " " + sys_b64 + " " + user_b64);

    char buffer[4096];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {false, "", "无法启动 Python 子进程（popen 失败）", 0};
    }

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    const int status = pclose(pipe);

    if (status != 0) {
        return {false, result, "Python 调用脚本退出码 " + std::to_string(status), 0};
    }
    return {true, result, "", 200};
}
