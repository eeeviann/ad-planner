#include "learning_log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

// 修复前这里用的是 <sys/time.h> + gettimeofday()，是 POSIX 专有接口，
// 在 Windows 上直接编译不过。改用 C++17 标准库的 chrono，跨平台。
static std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
    if (const std::tm* p = std::localtime(&t)) tm_buf = *p;

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << millis.count();
    return ss.str();
}

LearningLog::LearningLog(const std::string& log_path)
    : log_path_(log_path), total_attempts_(0), success_count_(0) {}

LearningLog::~LearningLog() {}

void LearningLog::log_attempt(int attempt_no, const std::string& final_model,
                              const std::string& problem) {
    total_attempts_ = attempt_no;
    current_model_ = final_model;
    current_problem_ = problem;
}

void LearningLog::mark_success(double obj_value) {
    success_count_++;
    current_obj_value_ = obj_value;
    current_error_ = "";

    LogEntry e;
    e.timestamp = timestamp();
    e.problem = current_problem_;
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
    e.attempt_count = total_attempts_;
    e.status = "failed";
    e.error = error;
    e.final_model = current_model_;
    e.objective_value = 0;
    entries_.push_back(e);
}

void LearningLog::save() {
    std::ofstream f(log_path_, std::ios::app);
    if (!f) return;
    for (const auto& e : entries_) {
        f << "=== " << e.timestamp << " ===\n";
        f << "Problem: " << e.problem << "\n";
        f << "Status: " << e.status << "\n";
        if (!e.error.empty()) f << "Error: " << e.error << "\n";
        f << "Attempts: " << e.attempt_count << "\n";
        f << "Final Model: " << e.final_model << "\n";
        f << "Objective: " << e.objective_value << "\n";
        f << "\n";
    }
    entries_.clear();
}
