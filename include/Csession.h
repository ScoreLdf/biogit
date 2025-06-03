#pragma once
#include <boost/asio.hpp>
#include "msg_node.h"

namespace Biogit {
class CServer;
class LogicSystem;
class Repository;


// CSession会话管理: 管理单个客户端的TCP连接，处理消息的收发和协议解析
class Csession : public std::enable_shared_from_this<Csession> {
    friend class LogicSystem;
public:
    /**
     * @brief CSession构造函数
     * @param io_context Boost.Asio的io_context，用于socket操作
     * @param server 指向CServer实例的指针，用于回调（如会话清理）和获取服务器配置
     * @param logic_system 指向LogicSystem实例的引用，用于投递待处理的消息
     */
    Csession(boost::asio::io_context& io_context,
             CServer* server,
             LogicSystem& logic_system);
    ~Csession();

    // 禁止拷贝和赋值
    Csession(const Csession&) = delete;
    Csession& operator=(const Csession&) = delete;

    /**
     * @brief 获取与此会话关联的TCP socket
     * @return boost::asio::ip::tcp::socket的引用
     */
    boost::asio::ip::tcp::socket& GetSocket();

    /**
     * @brief 获取此会话的唯一标识符 (UUID)
     * @return UUID字符串的const引用
     */
    const std::string& GetUuid() const;

    /**
     * @brief 启动会话，开始异步读取客户端数据
     */
    void Start();

    /**
     * @brief 关闭会话，关闭socket并通知CServer清理
     */
    void Close();

    /**
     * @brief 发送消息给客户端 (char*版本)
     * @param body_data 指向消息体数据的指针
     * @param body_length 消息体的长度 (字节)
     * @param msg_id 消息ID (来自biogit_protocol.h)
     */
    void Send(const char* body_data, uint32_t body_length, uint16_t msg_id);

    /**
     * @brief 发送消息给客户端 (std::string版本)
     */
    void Send(const std::string& body_data_str, uint16_t msg_id);

    /**
     * @brief 发送消息给客户端 (std::vector<char>版本)
     */
    void Send(const std::vector<char>& body_data_vec, uint16_t msg_id);

    /**
     * @brief 发送消息给客户端 (std::vector<std::byte>版本)
     */
    void Send(const std::vector<std::byte>& body_data_vec, uint16_t msg_id);

    /**
     * @brief 检查会话是否已关闭
     * @return 如果已关闭则为true，否则为false
     */
    bool IsClosed() const { return _is_closed.load(std::memory_order_acquire); } // memory_order_acquire用于读取

    // --- 会话特定仓库管理接口 ---
    /**
     * @brief 获取当前会话正在操作的Repository实例
     * @return 指向Repository的shared_ptr，如果未选择仓库则可能为nullptr
     */
    std::shared_ptr<Repository> GetActiveRepository() const;

    /**
     * @brief 检查当前会话是否已成功选择了一个仓库进行操作
     * @return 如果已选择仓库则为true，否则为false
     */
    bool IsRepositorySelected() const;

    /**
     * @brief 获取创建此会话的CServer实例的指针
     * 主要用于LogicSystem中的回调函数可能需要访问CServer的全局配置（如仓库根目录）
     * @return 指向CServer的指针
     */
    CServer* GetServer() { return _server; }

private:

    void SetActiveRepository(std::shared_ptr<Repository> repo, const std::string &repo_path_for_logging);
    /**
     * @brief 获取指向当前CSession对象的std::shared_ptr
     * 用于在异步回调中安全地传递this指针
     * @return std::shared_ptr<CSession>
     */
    std::shared_ptr<Csession> SharedSelf();

    /**
     * @brief 启动一次异步读取操作
     * 从socket读取数据到_recv_buffer
     */
    void AsyncRead();

    /**
     * @brief 处理异步读取操作完成的回调
     * @param error 错误码
     * @param bytes_transferred 实际读取到的字节数
     */
    void HandleRead(const boost::system::error_code& error, size_t bytes_transferred);

    /**
     * @brief 处理异步写入操作完成的回调
     * @param error 错误码
     */
    void HandleWrite(const boost::system::error_code& error);


    /**
     * @brief 处理已接收到_recv_buffer中的数据，进行协议解析（粘包/半包处理）
     * @param bytes_in_buffer _recv_buffer中本次HandleRead收到的有效字节数
     */
    void ProcessReceivedData(uint32_t bytes_in_buffer);

    /**
     * @brief 将一个完整的、已接收的消息（RecvNode）封装成LogicNode并投递给LogicSystem处理队列
     * @param complete_node 指向已完整接收的消息的RecvNode
     */
    void PostToLogicSystem(std::shared_ptr<RecvNode> complete_node);

    /**
     * @brief 直接在CSession中处理客户端发送的MSG_REQ_TARGET_REPO消息
     * 此方法会尝试加载客户端指定的仓库，并更新会话的_active_repository状态
     * @param body_data 指向MSG_REQ_TARGET_REPO消息体的指针 (内容是仓库的相对路径)
     * @param body_length 消息体的长度
     */
    void ProcessTargetRepoRequest(const char* body_data, uint32_t body_length);

    // --- 成员变量 ---
    boost::asio::ip::tcp::socket _socket; // 与客户端通信的socket
    std::string _uuid;                    // 会话的唯一标识符
    CServer* _server;                     // 指向创建此会话的CServer实例
    LogicSystem& _logic_system;           // 指向全局的LogicSystem实例

    std::atomic<bool> _is_closed;         // 标记会话是否已关闭 (atomic保证线程安全访问)
    std::array<char, 8192> _recv_buffer;  // 用于socket读取的原始数据缓冲区

    // 接收状态机相关成员
    bool _is_parsing_header;                            // true表示当前正在等待或解析消息头
    char _header_buffer[Protocol::HEAD_TOTAL_LEN];      // 用于缓存不足一个完整长度的消息头数据
    uint16_t _header_buffer_current_len;                // _header_buffer中当前已有的字节数
    std::shared_ptr<RecvNode> _current_processing_node; // 当前正在构建（接收消息体）的RecvNode

    // 会话特定的活动仓库信息
    std::shared_ptr<Repository> _active_repository;         // 客户端选定后，动态加载的仓库实例
    std::atomic<bool> _is_repository_selected;          // 标记客户端是否已成功选定仓库
    std::string _selected_repository_path_for_logging;  // 用于日志，记录当前服务仓库的路径

    // 发送队列及同步锁
    std::queue<std::shared_ptr<SendNode>> _send_queue;  // 待发送消息队列
    std::mutex _send_queue_mutex;                       // 保护_send_queue的互斥锁
    std::atomic<bool> _is_sending;                      // true表示当前有一个异步写操作正在进行
};


// LogicNode 用于将 CSession 和 RecvNode 传递给 LogicSystem
class LogicNode {
public:
    LogicNode(std::shared_ptr<Csession> session, std::shared_ptr<RecvNode> recv_node)
        : _session(std::move(session)), _recv_node(std::move(recv_node)) {} // 使用 std::move

    std::shared_ptr<Csession> _session;
    std::shared_ptr<RecvNode> _recv_node;
};

} // namespace Biogit