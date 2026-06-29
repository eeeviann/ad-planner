#ifndef LEARNING_LOG_H
#define LEARNING_LOG_H

#include <string>
#include <vector>
#include <fstream>
#include <ctime>

struct LogEntry {
    std::string timestamp;
    std::string problem;
    std::string llm_response;
    int attempt_count;
    std::string status;
    std::string error;
    std::string final_model;
    double objective_value;
};

class LearningLog {
public:
    LearningLog(const std::string& log_path);
    ~LearningLog();

    void log_attempt(int attempt_no, const std::string& model_json,
                     const std::string& model_str, const std::string& problem);
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
    std::string current_llm_response_;
    std::string current_error_;
    std::string current_model_;
    double current_obj_value_;
};

#endif
