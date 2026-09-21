#ifndef SHELL_COMMAND_H
#define SHELL_COMMAND_H

#include <string>

// 仅在必要时给参数加双引号（cmd.exe 与 sh 都适用）。
// 不加引号的情况能让 cmd.exe 正常解析，避免下面 for_shell() 要处理的怪癖。
inline std::string shell_quote(const std::string& s) {
    if (s.empty()) return "\"\"";
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    return "\"" + s + "\"";
}

// Windows 的 cmd.exe 有一个历史怪癖：当 /c 后面的命令以引号开头时，
// 它会先剥掉首尾两个引号再解析。于是
//     cmd /c "python" "solver/llm_call.py" ...
// 会被理解成程序名 `python" "solver/llm_call.py"`，报错：
//     'python" "solver' is not recognized as an internal or external command
// 规避办法是在最外层再套一对引号，让被剥掉的是外面这对。
inline std::string for_shell(const std::string& command) {
#ifdef _WIN32
    if (!command.empty() && command[0] == '"') return "\"" + command + "\"";
#endif
    return command;
}

#endif  // SHELL_COMMAND_H
