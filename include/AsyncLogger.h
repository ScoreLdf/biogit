#pragma once
#include "Singleton.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>

namespace Biogit {
// ---   日志消息结构体   ---
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::thread::id thread_id; // 记录产生日志的线程ID

    LogEntry(LogLevel lvl, std::string msg)
        : timestamp(std::chrono::system_clock::now()),
          level(lvl),
          message(std::move(msg)),
          thread_id(std::this_thread::get_id()) {}
};

// 将 LogLevel 转换为字符串的辅助函数
inline std::string LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

// 辅助函数，将时间点转换为特定格式的日期字符串 (YYYY-MM-DD)
inline std::string TimePointToDateString(const std::chrono::system_clock::time_point& tp) {
    std::time_t time_now = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_now);
#else
    localtime_r(&time_now, &tm_buf);
#endif
    char buffer[11]; // YYYY-MM-DD\0
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_buf);
    return std::string(buffer);
}


// 辅助函数，将时间点转换为特定格式的完整时间字符串 (YYYY-MM-DD HH:MM:SS)
inline std::string TimePointToDateTimeString(const std::chrono::system_clock::time_point& tp) {
    std::time_t time_now = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_now);
#else
    localtime_r(&time_now, &tm_buf);
#endif
    char buffer[20]; // YYYY-MM-DD HH:MM:SS\0
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buffer);
}


// ---  异步日志系统  ---
class AsyncLogger : public Singleton<AsyncLogger> {
    friend class Singleton<AsyncLogger>;

public:
    ~AsyncLogger();

    bool Start(const std::filesystem::path& log_directory,
               const std::string& log_file_base_name = "server_log",
               LogLevel min_level = LogLevel::INFO,
               bool console_output = true,
               size_t batch_size = 10);

    void Stop();

    void Log(LogLevel level, const std::string& message);
    void Log(LogLevel level, std::string&& message);

    void Debug(const std::string& message) { Log(LogLevel::DEBUG, message); }
    void Info(const std::string& message) { Log(LogLevel::INFO, message); }
    void Warn(const std::string& message) { Log(LogLevel::WARNING, message); }
    void Error(const std::string& message) { Log(LogLevel::ERROR, message); }
    void Fatal(const std::string& message) { Log(LogLevel::FATAL, message); }

private:
    AsyncLogger();

    void WorkerThreadFunction(); //
    std::string FormatLogEntry(const LogEntry& entry); //
    void OpenNewLogFileIfNeeded(const std::string& current_date_str); //

    std::thread m_workerThread; // 日志线程
    std::queue<LogEntry> m_logQueue; // 日志队列
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stopRequested; // 原子 是否停止
    std::atomic<bool> m_isStarted; // 原子 是否发生

    std::ofstream m_logFileStream;
    std::filesystem::path m_logDirectory; // 日志文件存放的目录
    std::string m_logFileBaseName; // 日志文件的基础名称 (不含日期和扩展名，例如 "biogit_server")
    std::string m_currentLogFileDateString; // 当前日志时间 拼接出完整日志文件名字

    LogLevel m_minLevel; // 最小info级别
    bool m_consoleOutput; // 是否输出控制台
    size_t m_batchSize; // 批处理大小
};


// 便捷宏
#define BIOGIT_LOG_DEBUG(msg) AsyncLogger::GetInstance()->Debug(msg)
#define BIOGIT_LOG_INFO(msg)  AsyncLogger::GetInstance()->Info(msg)
#define BIOGIT_LOG_WARN(msg)  AsyncLogger::GetInstance()->Warn(msg)
#define BIOGIT_LOG_ERROR(msg) AsyncLogger::GetInstance()->Error(msg)
#define BIOGIT_LOG_FATAL(msg) AsyncLogger::GetInstance()->Fatal(msg)


} // namespace Biogit