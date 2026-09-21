#ifndef LEARNING_LOG_H
#define LEARNING_LOG_H

#include <ctime>
#include <fstream>
#include <string>
#include <vector>

struct LogEntry {
    std::string timestamp;
    std::string problem;
    int attempt_count = 0;
    std::string status;
    std::string error;
    std::string final_model;
    double objective_value = 0.0;
};

// 运行日志。
//
// 日志文件默认写在仓库根目录的 learning_log.txt，并且已经在 .gitignore 中
// 排除，不会进入公开仓库。要彻底关闭记录，设置环境变量 ADPLANNER_LOG=off。
//
// 注意：这里只记录「提取后的模型 + 结论」，不再落原始 LLM 响应全文——
// 原始响应包含接口返回的 id 与用量统计，既无必要也容易误提交。
class LearningLog {
public:
    LearningLog(const std::string& log_path);
    ~LearningLog();

    void log_attempt(int attempt_no, const std::string& final_model,
                     const std::string& problem);
    void mark_success(double obj_value);
    void mark_failure(const std::string& error);
    void save();

    int total_attempts() const { return total_attempts_; }
    int success_count() const { return success_count_; }

private:
    std::string log_path_;
    std::vector<LogEntry> entries_;
    int total_attempts_;
    int success_count_;
    std::string current_problem_;
    std::string current_error_;
    std::string current_model_;
    double current_obj_value_ = 0.0;
};

#endif  // LEARNING_LOG_H
