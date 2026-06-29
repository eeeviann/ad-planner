#include "learning_log.h"
#include <sstream>
#include <iomanip>
#include <sys/time.h>
#include <fstream>

static std::string timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    std::ostringstream ss;
    std::time_t t = tv.tv_sec;
    std::tm* now = std::localtime(&t);
    ss << std::put_time(now, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << (tv.tv_usec / 1000);
    return ss.str();
}

LearningLog::LearningLog(const std::string& log_path)
    : log_path_(log_path), total_attempts_(0), success_count_(0) {}

LearningLog::~LearningLog() {}

void LearningLog::log_attempt(int attempt_no, const std::string& model_json,
                              const std::string& model_str, const std::string& problem) {
    total_attempts_++;
    current_llm_response_ = model_json;
    current_model_ = model_str;
    current_problem_ = problem;
}

void LearningLog::mark_success(double obj_value) {
    success_count_++;
    current_obj_value_ = obj_value;
    current_error_ = "";

    LogEntry e;
    e.timestamp = timestamp();
    e.problem = current_problem_;
    e.llm_response = current_llm_response_;
    e.attempt_count = total_attempts_;
    e.status = "success";
    e.error = "";
    e.final_model = current_model_;
    e.objective_value = obj_value;
    entries_.push_back(e);
}

void LearningLog::mark_failure(const std::string& error) {
    current_error_ = error;

    LogEntry e;
    e.timestamp = timestamp();
    e.problem = current_problem_;
    e.llm_response = current_llm_response_;
    e.attempt_count = total_attempts_;
    e.status = "failed";
    e.error = error;
    e.final_model = current_model_;
    e.objective_value = 0;
    entries_.push_back(e);
}

void LearningLog::save() {
    std::ofstream f(log_path_, std::ios::app);
    for (const auto& e : entries_) {
        f << "=== " << e.timestamp << " ===\n";
        f << "Problem: " << e.problem << "\n";
        f << "Status: " << e.status << "\n";
        if (!e.error.empty()) f << "Error: " << e.error << "\n";
        f << "Attempts: " << e.attempt_count << "\n";
        f << "LLM Response: " << e.llm_response << "\n";
        f << "Objective: " << e.objective_value << "\n";
        f << "\n";
    }
    f.close();
    entries_.clear();
}
