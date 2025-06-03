#pragma once
#include <filesystem>
#include <map>
#include <boost/asio.hpp>
#include <thread>

#include "Singleton.h"

namespace Biogit {
class Repository;
class Csession;


/**
 * @brief 单例异步IO服务池（奇异递归模版模式），通过GetInstance()返回IO池
 * @details
 *  通过创建了一个 io_context 池，每个 io_context 在一个独立的线程中运行 \n
 *  使用 executor_work_guard 来防止 io_context 在没有异步任务时自动退出，确保线程持续监听事件 \n
 *	通过 GetIOService() 以轮询 (round-robin) 方式分配 io_context给新的会话 (Session)，以实现负载均衡
 ***/
class AsioIOServicePool:public Singleton<AsioIOServicePool>
{
    friend Singleton<AsioIOServicePool>;

public:
    using IOService = boost::asio::io_context;
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    using WorkPtr = std::unique_ptr<WorkGuard>;

    ~AsioIOServicePool();
    AsioIOServicePool(const AsioIOServicePool&) = delete;
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;

    boost::asio::io_context& GetIOService();
    void Stop();

private:
    explicit AsioIOServicePool(std::size_t size = std::min(std::size_t(1),std::size_t(1)));
    std::vector<IOService> _ioServices;
    std::vector<WorkPtr> _works; // 使用 executor_work_guard 来防止 io_context 在没有异步任务时自动退出，确保线程持续监听事件
    std::vector<std::thread> _threads;
    std::size_t _nextIOService; // 轮询的index
};


class LogicSystem;      // 服务器需要 LogicSystem


/**
 * @brief 服务器端 监听客户端连接请求，并为每个连接创建一个会话 (CSession) 进行管理
 * @details
 *	构造：在指定的端口上创建一个acceptor，并开始异步等待连接 (StartAccept())
 *	StartAccept：从IO池获取 io_context 并异步监听，当有客户端连接时，创建一个CSession对象，调用HandleAccept()回调函数
 *	HandleAccept：调用 new_session->Start() 开始处理这个会话上的数据读取 ，并存储std::map (_sessions) 中，以便后续管理，再次调用 StartAccept() 以准备接受下一个连接
 *	ClearSession(): 当某个会话结束或出错时，从 _sessions 中移除它
 ***/
class CServer {
public:
    /**
     * @brief CServer 构造函数
     * @param pool AsioIOServicePool 的引用，用于获取 io_context
     * @param port 服务器监听的端口号
     * @param logic_system LogicSystem 的引用，将传递给 CSession
     * @param repos_root_path 服务器上所有 biogit2 仓库的根目录路径
     */
    CServer(AsioIOServicePool& pool,
            unsigned short port,
            LogicSystem& logic_system,
            const std::filesystem::path& repos_root_path); // 接收仓库根路径
    ~CServer();

    // 禁止拷贝和赋值
    CServer(const CServer&) = delete;
    CServer& operator=(const CServer&) = delete;

    /**
     * @brief 开始接受新的客户端连接。
     * 此方法会启动一个异步 accept 操作。
     */
    void StartAccept();

    /**
     * @brief 当一个 CSession 关闭时，由 CSession 调用此方法来通知 CServer。
     * CServer 会从其活动会话列表中移除该会话。
     * @param uuid 已关闭会话的唯一标识符。
     */
    void ClearSession(const std::string& uuid);

    /**
     * @brief 获取服务器配置的所有仓库的根目录路径。
     * CSession 在处理 MSG_REQ_TARGET_REPO 消息时会需要这个路径。
     * @return const std::filesystem::path& 仓库根目录路径的引用。
     */
    const std::filesystem::path& GetReposRootPath() const;

    /**
     * @brief 停止服务器接受新连接并尝试关闭所有现有连接。
     */
    void Stop();

private:
    /**
     * @brief 处理异步 accept 操作完成的回调函数。
     * @param new_session 新创建的 CSession 实例的共享指针。
     * @param error 操作的错误码。
     */
    void HandleAccept(std::shared_ptr<Csession> new_session, const boost::system::error_code& error);

    AsioIOServicePool& _io_service_pool;                // IO 服务池的引用
    boost::asio::ip::tcp::acceptor _acceptor;           // Boost.Asio 的 acceptor，用于监听和接受连接
    unsigned short _port;                               // 服务器监听的端口
    LogicSystem& _logic_system;                         // LogicSystem 实例的引用，传递给 CSession
    const std::filesystem::path _repos_root_path;       // 所有仓库的根目录

    std::map<std::string, std::shared_ptr<Csession>> _sessions; // 存储当前所有活动的 CSession
    std::mutex _sessions_mutex;                         // 用于保护对 _sessions map 的并发访问
    std::atomic<bool> _is_stopped;                      // 标记服务器是否已停止接受新连接
};


}



