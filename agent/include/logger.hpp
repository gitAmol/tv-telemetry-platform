#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace telemetry {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(LogLevel level) { min_level_ = level; }

    void set_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.open(path, std::ios::app);
    }

    template<typename... Args>
    void log(LogLevel level, const char* file, int line, Args&&... args) {
        if (level < min_level_) return;
        std::ostringstream msg;
        (msg << ... << args);
        write(level, file, line, msg.str());
    }

private:
    Logger() = default;

    void write(LogLevel level, const char* file, int line, const std::string& msg) {
        auto now   = std::chrono::system_clock::now();
        auto tt    = std::chrono::system_clock::to_time_t(now);
        auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch()) % 1000;
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        // extract filename only
        std::string f = file;
        auto slash = f.find_last_of("/\\");
        if (slash != std::string::npos) f = f.substr(slash + 1);

        std::ostringstream entry;
        entry << "[" << std::put_time(&tm, "%H:%M:%S")
              << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
              << "[" << label(level) << "]"
              << "[" << f << ":" << line << "] " << msg;

        std::lock_guard<std::mutex> lock(mutex_);
        (level >= LogLevel::ERROR ? std::cerr : std::cout) << entry.str() << "\n";
        if (file_.is_open()) file_ << entry.str() << "\n";
    }

    static const char* label(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
        }
        return "?????";
    }

    LogLevel   min_level_{LogLevel::INFO};
    std::mutex mutex_;
    std::ofstream file_;
};

} // namespace telemetry

#define LOG_DEBUG(...) telemetry::Logger::instance().log(telemetry::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  telemetry::Logger::instance().log(telemetry::LogLevel::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  telemetry::Logger::instance().log(telemetry::LogLevel::WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) telemetry::Logger::instance().log(telemetry::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
