//
// Created by maxim on 28.06.2024.
//
#include "../../include/Core/Logger.h"
#include <iostream>
#include <ctime>

namespace Shoonyakasha {

Logger::Logger(const std::string& filename, size_t maxFileSize)
        : m_logFile(filename, std::ios::app)
        , m_currentLogLevel(LogLevel::Info)
        , m_maxFileSize(maxFileSize)
        , m_currentFileSize(0)
        , m_filename(filename) {
    if (m_logFile.is_open()) {
        m_logFile.seekp(0, std::ios::end);
        m_currentFileSize = m_logFile.tellp();
    }
}

Logger::~Logger() {
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

void Logger::logMessage(LogLevel level, const std::string& message) {
    if (level < m_currentLogLevel) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string levelStr;
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO"; break;
        case LogLevel::Warning: levelStr = "WARNING"; break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
    }

    std::string timestamp = getCurrentTimestamp();
    std::string fullMessage = timestamp + " [" + levelStr + "] " + message + "\n";

    m_logFile << fullMessage;
    m_logFile.flush();

    m_currentFileSize += fullMessage.size();
    if (m_currentFileSize >= m_maxFileSize) {
        rotateLogFile();
    }

    // Also print to console for immediate feedback
    std::cout << fullMessage;
}

bool Logger::shouldLogThrottled(const char* key, float intervalSeconds) {
    auto now = std::chrono::steady_clock::now();
    auto it = m_throttleTimestamps.find(static_cast<const void*>(key));
    if (it != m_throttleTimestamps.end()) {
        auto elapsed = std::chrono::duration<float>(now - it->second).count();
        if (elapsed < intervalSeconds) return false;
        it->second = now;
    } else {
        m_throttleTimestamps[static_cast<const void*>(key)] = now;
    }
    return true;
}

void Logger::rotateLogFile() {
    m_logFile.close();

    std::string newFilename = m_filename + "." + getCurrentTimestamp();
    std::rename(m_filename.c_str(), newFilename.c_str());

    m_logFile.open(m_filename, std::ios::app);
    m_currentFileSize = 0;
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

} // namespace Shoonyakasha