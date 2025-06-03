#include "../include/IoServicePool.h"
#include "../include/AsyncLogger.h"

#include <iostream>
#include "Csession.h"


namespace Biogit {

AsioIOServicePool::AsioIOServicePool(std::size_t size): _ioServices(size), _works(size), _nextIOService(0) {
    if (size == 0) {
        throw std::runtime_error("AsioIOServicePool size cannot be zero.");
    }
    // 使用 executor_work_guard 来防止 io_context 在没有异步任务时自动退出，确保线程持续监听事件
    for (std::size_t i = 0; i < size; ++i) {
        _works[i] = std::make_unique<WorkGuard>(boost::asio::make_work_guard(_ioServices[i]));
    }
    // 每个 io_context 在一个独立的线程中运行
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            try {
                _ioServices[i].run();
            } catch (const std::exception& e) {
                std::cerr << "Exception in AsioIOServicePool thread " << i << ": " << e.what() << std::endl;
            }
        });
    }
    BIOGIT_LOG_INFO("AsioIOServicePool constructed");
}


AsioIOServicePool::~AsioIOServicePool() {
    BIOGIT_LOG_INFO( "AsioIOServicePool destructing...");
    Stop(); // 考虑是否在这里调用，或依赖外部调用
    BIOGIT_LOG_INFO("AsioIOServicePool destructed.");
}

boost::asio::io_context& AsioIOServicePool::GetIOService() {
    boost::asio::io_context& service = _ioServices[_nextIOService];  // 简单的轮询
    _nextIOService = (_nextIOService + 1) % _ioServices.size();
    return service;
}

void AsioIOServicePool::Stop() {
    std::cout << "AsioIOServicePool stopping..." << std::endl;
    // 首先重置 work 对象，允许 io_context::run() 在完成当前任务后退出
    for (auto& work : _works) {
        if (work) { // 确保 work 指针有效
            work.reset(); // 销毁 work guard，io_context::run() 将在没有更多工作时返回
        }
    }

    // 然后停止所有 io_context (这会导致阻塞的 run() 调用返回)
    for (auto& service : _ioServices) {
        if (!service.stopped()) {
            service.stop();
        }
    }

    // 等待所有线程结束
    for (auto& t : _threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    _threads.clear(); // 清空线程容器
    BIOGIT_LOG_INFO("AsioIOServicePool stopped.");
}


CServer::CServer(AsioIOServicePool& pool,
                 unsigned short port,
                 LogicSystem& logic_system,
                 const std::filesystem::path& repos_root_path) // 接收仓库根路径
    : _io_service_pool(pool),
      _acceptor(_io_service_pool.GetIOService()), // 从池中获取一个 io_context 给 acceptor
      _port(port),
      _logic_system(logic_system),
      _repos_root_path(repos_root_path), // 初始化仓库根路径
      _is_stopped(false) {

    // 检查仓库根路径是否有效且存在
    std::error_code ec_fs;
    if (_repos_root_path.empty()) {
        BIOGIT_LOG_ERROR("CServer Error: Repositories root path cannot be empty.");
        throw std::invalid_argument("Repositories root path is empty.");
    }
    if (!std::filesystem::exists(_repos_root_path, ec_fs)) {
        BIOGIT_LOG_INFO("CServer Info: Repositories root path" + _repos_root_path.string() + " does not exist. Attempting to create it.");
        if (!std::filesystem::create_directories(_repos_root_path, ec_fs)) {
            BIOGIT_LOG_ERROR("CServer Error: Failed to create repositories root directory " + _repos_root_path.string() + " - " + ec_fs.message()) ;
            throw std::runtime_error("Failed to create repositories root directory: " + ec_fs.message());
        }
    } else if (!std::filesystem::is_directory(_repos_root_path, ec_fs)) {
        BIOGIT_LOG_ERROR("CServer Error: Repositories root path " +  _repos_root_path.string() + " exists but is not a directory.");
        throw std::runtime_error("Repositories root path is not a directory.");
    }
    if (ec_fs) { // 捕捉 exists 或 is_directory 的其他错误
        BIOGIT_LOG_ERROR( "CServer Error: Filesystem error checking repositories root path "
                  + _repos_root_path.string() + " - " + ec_fs.message() );
        throw std::runtime_error("Filesystem error with repositories root path: " + ec_fs.message());
    }

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), _port);
    boost::system::error_code ec;

    // 1. 打开 acceptor
    _acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        std::cerr << "CServer Error: Failed to open acceptor: " << ec.message() << std::endl;
        throw boost::system::system_error(ec); // 抛出异常，让上层知道启动失败
    }

    // 2. 设置 SO_REUSEADDR 选项，允许服务器快速重启并重新绑定到之前的端口
    _acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        std::cerr << "CServer Error: Failed to set SO_REUSEADDR option: " << ec.message() << std::endl;
        _acceptor.close(); // 关闭已打开的 acceptor
        throw boost::system::system_error(ec);
    }

    // 3. 绑定 acceptor 到指定的地址和端口
    _acceptor.bind(endpoint, ec);
    if (ec) {
        std::cerr << "CServer Error: Failed to bind to port " << _port << ": " << ec.message() << std::endl;
        _acceptor.close();
        throw boost::system::system_error(ec);
    }

    // 4. 开始监听连接请求
    _acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::cerr << "CServer Error: Failed to listen on port " << _port << ": " << ec.message() << std::endl;
        _acceptor.close();
        throw boost::system::system_error(ec);
    }
    std::cout << "CServer: Successfully initialized and listening on port " << _port << std::endl;
    std::cout << "CServer: Serving repositories from root directory: " << _repos_root_path.string() << std::endl;

    StartAccept();
}

CServer::~CServer() {
    std::cout << "CServer: Destructing (port: " << _port << ")" << std::endl;
    // 确保服务器在析构时已停止，避免资源泄露或未完成的操作
    if (!_is_stopped.load(std::memory_order_acquire)) {
        Stop(); // 调用Stop来执行清理
    }
}

void CServer::StartAccept() {
    // 如果服务器已标记为停止，或者acceptor未打开，则不启动新的接受操作
    if (_is_stopped.load(std::memory_order_acquire) || !_acceptor.is_open()) {
        return;
    }

    try {
        // 为即将到来的新连接创建一个CSession实例  从池中获取io_context 并传入CServer的指针(this)和LogicSystem的引用
        auto new_session = std::make_shared<Csession>(
            _io_service_pool.GetIOService(), // 为新会话从池中获取一个io_context
            this,                            // 传递指向当前CServer实例的指针
            _logic_system                    // 传递LogicSystem的引用
        );

        // 异步等待并接受一个新的连接， 当连接被接受或发生错误时，会调用CServer::HandleAccept方法。
        _acceptor.async_accept(new_session->GetSocket(),
                               std::bind(&CServer::HandleAccept, this, new_session,
                                         std::placeholders::_1 // _1 对应HandleAccept的error_code参数
                                        ));
    } catch (const std::exception& e) {
        std::cerr << "CServer: Exception in StartAccept initiating async_accept: " << e.what() << std::endl;
        // Stop(); // 发生异常，可能需要考虑停止服务器或进行错误恢复
    }
}

void CServer::HandleAccept(std::shared_ptr<Csession> new_session, const boost::system::error_code& error) {
    // 如果服务器已标记为停止，则不处理新接受的连接（它可能是在Stop()调用后完成的）
    if (_is_stopped.load(std::memory_order_acquire)) {
        if (new_session && new_session->GetSocket().is_open()) {
            boost::system::error_code ec_close;
            new_session->GetSocket().close(ec_close); // 尝试关闭socket
        }
        return;
    }

    if (!error) { // 如果接受连接没有错误
        std::cout << "CServer: New connection accepted. Session UUID: " << new_session->GetUuid() << std::endl;
        new_session->Start(); // 启动新会话，开始异步读取数据
        {
            std::lock_guard<std::mutex> lock(_sessions_mutex); // 保护对_sessions map的访问
            _sessions[new_session->GetUuid()] = new_session;   // 将新会话加入到活动会话列表
        }
        std::cout << "CServer: Active sessions: " << _sessions.size() << std::endl;
    } else { // 如果接受连接时发生错误
        // operation_aborted 错误通常发生在acceptor被关闭时（例如服务器停止过程中）
        if (error != boost::asio::error::operation_aborted) {
             std::cerr << "CServer: Accept error: " << error.message() << std::endl;
        }
        // new_session 的 shared_ptr 在这里会超出作用域，如果引用计数为0则会被销毁。
        // 它关联的 socket 可能没有被成功打开，或者 HandleAccept 在出错时 socket 已经是关闭状态。
    }

    // 只要acceptor仍然打开且服务器未停止，就继续准备接受下一个连接
    if (_acceptor.is_open() && !_is_stopped.load(std::memory_order_acquire)) {
        StartAccept();
    } else {
        // std::cout << "CServer: Acceptor is closed or server stopped, not initiating new accept." << std::endl;
    }
}

void CServer::ClearSession(const std::string& uuid) {
    // std::cout << "CServer: Attempting to clear session " << uuid << std::endl;
    std::lock_guard<std::mutex> lock(_sessions_mutex); // 保护对_sessions map的访问
    _sessions.erase(uuid); // 从活动会话列表中移除指定的会话
    std::cout << "CServer: Session " << uuid << " cleared. Active sessions: " << _sessions.size() << std::endl;
}

const std::filesystem::path& CServer::GetReposRootPath() const {
    return _repos_root_path;
}

void CServer::Stop() {
    // 使用atomic的exchange确保Stop逻辑只执行一次，防止并发调用问题
    if (_is_stopped.exchange(true, std::memory_order_acq_rel)) {
        return; // 如果之前已经是true (已停止)，则直接返回
    }
    std::cout << "CServer: Stopping server on port " << _port << "..." << std::endl;

    boost::system::error_code ec;
    // 1. 关闭acceptor，不再接受新的连接
    if (_acceptor.is_open()) {
        _acceptor.cancel(ec); // 取消所有挂起的异步accept操作
        // if(ec) std::cerr << "CServer: Error cancelling acceptor: " << ec.message() << std::endl;
        _acceptor.close(ec);  // 关闭acceptor
        // if(ec) std::cerr << "CServer: Error closing acceptor: " << ec.message() << std::endl;
        // else std::cout << "CServer: Acceptor closed." << std::endl;
    }

    // 2. 关闭所有当前活动的会话
    //    为避免在遍历_sessions时修改它（CSession::Close可能会调用ClearSession），
    //    先复制一份需要关闭的会话列表。
    std::vector<std::shared_ptr<Csession>> sessions_to_close;
    {
        std::lock_guard<std::mutex> lock(_sessions_mutex);
        for (auto const& [uuid, session_ptr] : _sessions) {
            sessions_to_close.push_back(session_ptr);
        }
        // _sessions.clear(); // 可以在这里清空，或者让ClearSession逐个移除
                           // 如果在这里clear，ClearSession中的erase会找不到，没关系
    }

    std::cout << "CServer: Closing " << sessions_to_close.size() << " active session(s)..." << std::endl;
    for (auto& session : sessions_to_close) {
        if (session && !session->IsClosed()) {
            session->Close(); // 调用CSession的Close方法
        }
    }
    // 等待一小段时间，让会话的关闭操作有机会完成（可选，或者依赖于更复杂的同步机制）
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 再次检查并清空 _sessions，以防在上述关闭过程中有新的 ClearSession 调用未完全处理
    {
        std::lock_guard<std::mutex> lock(_sessions_mutex);
        _sessions.clear();
    }

    std::cout << "CServer: Stopped." << std::endl;
}




}
