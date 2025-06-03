#include "AsyncLogger.h"

namespace Biogit {

AsyncLogger::AsyncLogger()
    : m_stopRequested(false),
      m_isStarted(false),
      m_logFileBaseName("biogit_server"), // 默认基础名
      m_minLevel(LogLevel::INFO), // 默认info级别
      m_consoleOutput(true), // 默认输出到控制台
      m_batchSize(10) { // 默认批处理大小
}

AsyncLogger::~AsyncLogger() {
    Stop();
}

/**
 * @brief 初始化并启动日志系统。
 * @param log_directory 日志文件存放的目录。
 * @param log_file_base_name 日志文件的基础名称 (不含日期和扩展名，例如 "biogit_server")。
 * @param min_level 要记录的最低日志级别。
 * @param console_output 是否也输出到控制台。
 * @param batch_size 累积多少条日志后尝试写入文件。
 * @return 如果成功启动则为 true。
 */
bool AsyncLogger::Start(const std::filesystem::path& log_directory,
                        const std::string& log_file_base_name,
                        LogLevel min_level,
                        bool console_output,
                        size_t batch_size) {


    if (m_isStarted.load()) {
        std::cerr << "AsyncLogger: Already started." << std::endl;
        return true;
    }

    m_logDirectory = log_directory;
    m_logFileBaseName = log_file_base_name;
    m_minLevel = min_level;
    m_consoleOutput = console_output;
    m_batchSize = (batch_size == 0) ? 1 : batch_size; // 确保 batch_size 至少为1

    // 确保日志文件目录存在
    if (!m_logDirectory.empty() && !std::filesystem::exists(m_logDirectory)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(m_logDirectory, ec)) {
            std::cerr << "AsyncLogger Error: Failed to create log directory: "
                      << m_logDirectory.string() << " - " << ec.message() << std::endl;
            return false;
        }
    }

    // 初始打开日志文件 (基于当前日期)
    m_currentLogFileDateString = TimePointToDateString(std::chrono::system_clock::now());
    OpenNewLogFileIfNeeded(m_currentLogFileDateString); // 会尝试打开文件
    if (!m_logFileStream.is_open()) {
        // OpenNewLogFileIfNeeded 内部已打印错误
        return false;
    }

    m_stopRequested.store(false);
    m_workerThread = std::thread(&AsyncLogger::WorkerThreadFunction, this);
    m_isStarted.store(true);
    // Log方法记录启动信息
    this->Log(LogLevel::INFO, "AsyncLogger started. Logging to directory: " + m_logDirectory.string() + ", Base name: " + m_logFileBaseName);
    return true;
}

void AsyncLogger::Stop() {
    if (!m_isStarted.exchange(false)) {
        return;
    }
    // Log方法记录停止信息
    this->Log(LogLevel::INFO, "AsyncLogger stopping...");
    m_stopRequested.store(true);
    m_condition.notify_one();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (m_logFileStream.is_open()) {
        m_logFileStream.flush(); // 确保所有缓冲的日志都写入
        m_logFileStream.close();
    }
    // 直接输出到控制台
    std::cout << "AsyncLogger: Service stopped and thread joined. Final log file: " << (m_logDirectory / (m_logFileBaseName + "_" + m_currentLogFileDateString + ".log")).string() << std::endl;
}

void AsyncLogger::Log(LogLevel level, const std::string& message) {
    if (!m_isStarted.load(std::memory_order_relaxed) || level < m_minLevel) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_logQueue.emplace(level, message);
    }
    m_condition.notify_one();
}

void AsyncLogger::Log(LogLevel level, std::string&& message){
    if (!m_isStarted.load(std::memory_order_relaxed) || level < m_minLevel) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_logQueue.emplace(level, std::move(message));
    }
    m_condition.notify_one();
}

// 打开新的日志文件，如果日期发生变化或当前文件未打开
void AsyncLogger::OpenNewLogFileIfNeeded(const std::string& current_date_str) {
    if (current_date_str != m_currentLogFileDateString || !m_logFileStream.is_open()) {
        if (m_logFileStream.is_open()) { // 安全关闭当前文件
            m_logFileStream.flush();
            m_logFileStream.close();
        }
        m_currentLogFileDateString = current_date_str;
        std::filesystem::path new_log_file_path = m_logDirectory / (m_logFileBaseName + "_" + m_currentLogFileDateString + ".log");

        m_logFileStream.open(new_log_file_path, std::ios::app | std::ios::out);
        if (!m_logFileStream.is_open()) {
            std::cerr << "AsyncLogger Error: Failed to open new log file: " << new_log_file_path.string() << std::endl;
            // TODO ： 应该打开备用日志文件
        } else {
            if(m_isStarted.load()){ // 只有在系统已启动后才记录切换日志
                 this->Log(LogLevel::INFO, "Switched to new log file: " + new_log_file_path.string());
            }
        }
    }
}


std::string AsyncLogger::FormatLogEntry(const LogEntry& entry) {
    std::ostringstream oss_tid;
    oss_tid << entry.thread_id; // 获取线程ID字符串

    std::ostringstream oss;
    oss << TimePointToDateTimeString(entry.timestamp) << " "
        << "[" << LogLevelToString(entry.level) << "] "
        << "[Thread:" << oss_tid.str() << "] "
        << entry.message << std::endl; // 每条日志自动换行
    return oss.str();
}


void AsyncLogger::WorkerThreadFunction() {
    std::vector<LogEntry> local_batch; // 用于批量处理日志
    local_batch.reserve(m_batchSize + 10); // 预留一些空间

    std::string last_processed_date_str = TimePointToDateString(std::chrono::system_clock::now());
    OpenNewLogFileIfNeeded(last_processed_date_str); // 线程启动时确保日志文件已打开

    std::cout << "AsyncLogger: Worker thread started. Initial log date: " << last_processed_date_str << std::endl;


    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            // 等待直到被通知，或者停止请求且队列为空
            m_condition.wait(lock, [this] {
                return m_stopRequested.load(std::memory_order_relaxed) || !m_logQueue.empty();
            });

            if (m_stopRequested.load(std::memory_order_relaxed) && m_logQueue.empty()) {
                break; // 收到停止请求且队列已空，退出
            }

            // 从主队列批量移动到本地队列，减少锁的持有时间
            size_t count = 0;
            while(!m_logQueue.empty() && count < m_batchSize) {
                local_batch.push_back(std::move(m_logQueue.front()));
                m_logQueue.pop();
                count++;
            }
        }

        if (!local_batch.empty()) {
            // 检查是否需要轮转日志文件 (基于当前批次中第一条日志的时间戳)  可能导致同一批次的日志跨越午夜时被写入两个文件，但实现简单
            std::string current_batch_date_str = TimePointToDateString(local_batch.front().timestamp);
            if (current_batch_date_str != last_processed_date_str) {
                OpenNewLogFileIfNeeded(current_batch_date_str);
                last_processed_date_str = current_batch_date_str; // 更新最后处理的日期
            }

            if (m_logFileStream.is_open()) {
                for (const auto& log_item : local_batch) {
                    std::string formatted_entry = FormatLogEntry(log_item);
                    if (m_consoleOutput) { // 如果配置了控制台输出
                        if (log_item.level >= LogLevel::WARNING) { // 警告及以上级别输出到 cerr
                            std::cerr << formatted_entry;
                        } else { // INFO, DEBUG 输出到 cout
                            std::cout << formatted_entry;
                        }
                    }
                    m_logFileStream << formatted_entry; // 写入文件
                }
                m_logFileStream << std::flush; // 每次批量写入后刷新到磁盘
            } else {
                // 如果文件流意外关闭（例如在OpenNewLogFileIfNeeded中失败），至少尝试输出到控制台（如果启用了）
                if (m_consoleOutput) {
                    std::cerr << "AsyncLogger Error: Log file stream is not open. Dumping " << local_batch.size() << " log entries to console:" << std::endl;
                    for (const auto& log_item : local_batch) {
                         std::cerr << FormatLogEntry(log_item);
                    }
                }
            }
            local_batch.clear();
        }
        // 再次检查停止条件，确保在处理完一批后，如果收到停止信号且队列已空，能及时退出
        if (m_stopRequested.load(std::memory_order_relaxed) && m_logQueue.empty()){
            break;
        }

    }
    std::cout << "AsyncLogger: Worker thread finished." << std::endl;
}

}