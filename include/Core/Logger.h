//
// Created by maxim on 28.06.2024.
//
#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <unordered_map>

namespace Shoonyakasha {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    explicit Logger(const std::string& filename, size_t maxFileSize = 5 * 1024 * 1024);
    ~Logger();

    template<typename... Args>
    void log(LogLevel level, const char* format, Args... args) {
        // Global interval throttle: Info/Debug auto-throttled per call site
        if (m_globalInterval > 0.f && level <= LogLevel::Info) {
            if (!shouldLogThrottled(format, m_globalInterval)) return;
        }
        std::string message = formatString(format, args...);
        logMessage(level, message);
    }

    // Throttled logging: emits the message at most once every `intervalSeconds`.
    // Uses the format string pointer as a key, so each call site is throttled independently.
    template<typename... Args>
    void logEvery(float intervalSeconds, LogLevel level, const char* format, Args... args) {
        if (!shouldLogThrottled(format, intervalSeconds)) return;
        std::string message = formatString(format, args...);
        logMessage(level, message);
    }

    void setLogLevel(LogLevel level) { m_currentLogLevel = level; }
    LogLevel getLogLevel() const { return m_currentLogLevel; }

    /// Set global throttle interval for Info/Debug messages (0 = disabled)
    /// Each unique call site is throttled independently
    void setLogInterval(float seconds) { m_globalInterval = seconds; }
    float getLogInterval() const { return m_globalInterval; }

private:
    void logMessage(LogLevel level, const std::string& message);
    bool shouldLogThrottled(const char* key, float intervalSeconds);
    void rotateLogFile();
    std::string getCurrentTimestamp();

    template<typename... Args>
    std::string formatString(const char* format, Args... args) {
        int size = snprintf(nullptr, 0, format, args...);
        std::string result(size + 1, '\0');
        snprintf(&result[0], size + 1, format, args...);
        result.pop_back();
        return result;
    }

    std::ofstream m_logFile;
    std::mutex m_mutex;
    LogLevel m_currentLogLevel;
    size_t m_maxFileSize;
    size_t m_currentFileSize;
    std::string m_filename;
    float m_globalInterval = 0.f;  // Global throttle interval (0 = disabled)
    std::unordered_map<const void*, std::chrono::steady_clock::time_point> m_throttleTimestamps;
};

} // namespace Shoonyakasha
using Shoonyakasha::LogLevel;
using Shoonyakasha::Logger;

