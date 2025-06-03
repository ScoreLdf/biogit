#include "../include/Csession.h"
#include "../include/IoServicePool.h"
#include "../include/LogicSystem.h"
#include "../include/Repository.h"

#include <iostream>
#include <filesystem>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace Biogit {

Csession::Csession(boost::asio::io_context& io_context,
                 CServer* server,
                 LogicSystem& logic_system)
    : _socket(io_context),                  // 初始化socket
      _server(server),                      // 保存CServer指针
      _logic_system(logic_system),          // 保存LogicSystem引用
      _is_closed(false),                    // 初始状态：未关闭
      _is_parsing_header(true),             // 初始状态：准备解析消息头
      _header_buffer_current_len(0),        // 头部缓冲区当前为空
      _active_repository(nullptr),          // 初始状态：未选择任何活动仓库
      _is_repository_selected(false),       // 初始状态：仓库未选定
      _is_sending(false) {                  // 初始状态：没有正在进行的发送操作
    boost::uuids::uuid a_uuid = boost::uuids::random_generator()(); // 生成唯一ID
    _uuid = boost::uuids::to_string(a_uuid);
    std::cout << "CSession [" << _uuid << "]: Created." << std::endl;
}

Csession::~Csession() {
    std::cout << "CSession [" << _uuid << "]: Destructing (served repo: '" << _selected_repository_path_for_logging << "')..." << std::endl;
    // 确保在对象销毁前，socket和资源得到正确关闭和清理
    if (!_is_closed.load(std::memory_order_acquire)) { // memory_order_acquire用于读取atomic bool
        Close();
    }
    std::cout << "CSession [" << _uuid << "]: Destructed." << std::endl;
}

boost::asio::ip::tcp::socket& Csession::GetSocket() {
    return _socket;
}

const std::string& Csession::GetUuid() const {
    return _uuid;
}

std::shared_ptr<Csession> Csession::SharedSelf() {
    // enable_shared_from_this允许在类成员函数内部安全地获取指向自身的shared_ptr
    // 这对于将this指针绑定到异步回调非常重要，可以保证对象在回调执行时仍然存活
    return shared_from_this();
}

void Csession::Start() {
    if (_is_closed.load(std::memory_order_acquire)) { // 如果已关闭，则不执行任何操作
        return;
    }
    std::cout << "CSession [" << _uuid << "]: Starting session, beginning to read." << std::endl;
    AsyncRead(); // 开始第一次异步读取
}

void Csession::AsyncRead() {
    if (_is_closed.load(std::memory_order_acquire)) {
        return;
    }
    // 异步从socket读取一些数据到_recv_buffer  当读取完成（成功或失败）时，会调用HandleRead回调函数
    _socket.async_read_some(boost::asio::buffer(_recv_buffer),
                            std::bind(&Csession::HandleRead, SharedSelf(), // 使用SharedSelf()传递this
                                      std::placeholders::_1, // 对应error_code
                                      std::placeholders::_2  // 对应bytes_transferred
                                     ));
}

void Csession::Close() {
    // 使用atomic的exchange确保只执行一次关闭逻辑
    if (_is_closed.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::cout << "CSession [" << _uuid << "]: Closing session and socket." << std::endl;

    boost::system::error_code ec;
    if (_socket.is_open()) {
        // 优雅地关闭socket：先shutdown，再close
        _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        _socket.close(ec);
    }

    // 通知CServer实例清理此会话的记录
    if (_server) {
        _server->ClearSession(_uuid);
    }
}

// --- 发送逻辑 ---
void Csession::Send(const char* body_data, uint32_t body_length, uint16_t msg_id) {
    // 1. 检查会话是否已关闭
    if (_is_closed.load(std::memory_order_acquire)) {
        std::cerr << "CSession [" << _uuid << "]: Attempted to send on a closed session (msg_id: " << msg_id << ")." << std::endl;
        return;
    }

    // 2. 创建一个SendNode，它会自动打包消息头和消息体
    auto send_node = std::make_shared<SendNode>(body_data, body_length, msg_id);

    // 3. 控制对发送队列的访问并决定是否立即启动发送
    bool should_initiate_send = false;
    {
        std::lock_guard<std::mutex> lock(_send_queue_mutex); // 保护发送队列
        _send_queue.push(send_node); // 将待发送节点加入队列

        // 核心逻辑：如果当前没有其他异步写操作正在进行，那么这个 Send 调用就需要负责启动它
        // _is_sending 状态用于防止同时发起多个async_write调用
        if (!_is_sending.load(std::memory_order_acquire)) { // 检查当前是否没有正在进行的发送
            _is_sending.store(true, std::memory_order_release); // 原子地设置为true，标记“现在开始有发送操作了”
            should_initiate_send = true; // 标记当前这个 Send 调用需要启动异步写
        }
    }

    // 4. 如果需要，启动异步发送操作
    if (should_initiate_send) {
        std::shared_ptr<SendNode> node_to_send_now; // 用于存储要发送的节点
        {
            std::lock_guard<std::mutex> lock(_send_queue_mutex);
            if(!_send_queue.empty()) { // 再次检查，以防极端并发情况
                node_to_send_now = _send_queue.front();
            }
            // pop() 的操作会在 HandleWrite 中发送成功后进行
        }
        if(node_to_send_now){ // 成功获取到要发送的节点
             // 当发送完成（成功或失败）时，会调用 CSession::HandleWrite 方法
             boost::asio::async_write(_socket,
                                 boost::asio::buffer(node_to_send_now->data(), node_to_send_now->total_length()),
                                 std::bind(&Csession::HandleWrite, SharedSelf(), std::placeholders::_1));
        } else { // 队列是空的，但是 _is_sending 被设为 true 了，这不应该发生
             std::lock_guard<std::mutex> lock(_send_queue_mutex);
             _is_sending.store(false, std::memory_order_release); // 重置发送状态
        }
    }
}
void Csession::Send(const std::string& body_data_str, uint16_t msg_id) { Send(body_data_str.c_str(), static_cast<uint32_t>(body_data_str.length()), msg_id); }
void Csession::Send(const std::vector<char>& body_data_vec, uint16_t msg_id) { Send(body_data_vec.data(), static_cast<uint32_t>(body_data_vec.size()), msg_id); }
void Csession::Send(const std::vector<std::byte>& body_data_vec, uint16_t msg_id) { Send(reinterpret_cast<const char*>(body_data_vec.data()), static_cast<uint32_t>(body_data_vec.size()), msg_id); }

void Csession::HandleWrite(const boost::system::error_code& error) {
    // 1. 检查会话是否已在其他地方被关闭
    if (_is_closed.load(std::memory_order_acquire)) {
        return;
    }

    // 2. 检查上一次的 async_write 操作是否成功
    if (!error) {
        std::shared_ptr<SendNode> next_send_node = nullptr;  // 用于存储下一个要发送的消息节点
        bool no_more_to_send = false;
        {
            std::lock_guard<std::mutex> lock(_send_queue_mutex);
            // 3.1 移除已发送的消息
            if (!_send_queue.empty()) {
                _send_queue.pop();
            } else {
                // 这通常不应该发生，因为HandleWrite是在一个成功的async_write之后调用的
                std::cerr << "CSession [" << _uuid << "]: HandleWrite called, but send queue was already empty!" << std::endl;
            }
            // 3.2 检查队列中是否还有待发送的消息
            if (!_send_queue.empty()) {
                next_send_node = _send_queue.front(); // 获取下一个待发送的消息
            } else {
                _is_sending.store(false, std::memory_order_release); // 队列已空，重置发送状态
                no_more_to_send = true;
            }
        }
        // 4. 如果队列中还有消息，则启动下一次异步发送
        if (next_send_node) {
            boost::asio::async_write(_socket,
                                     boost::asio::buffer(next_send_node->data(), next_send_node->total_length()),
                                     std::bind(&Csession::HandleWrite, SharedSelf(), std::placeholders::_1));
        } else if (no_more_to_send) {
            std::cout << "CSession [" << _uuid << "]: Send queue is now empty." << std::endl;
        }
    } else {
        std::cerr << "CSession [" << _uuid << "]: HandleWrite error: " << error.message() << std::endl;
        _is_sending.store(false, std::memory_order_release); // 发送出错，也需要重置发送状态
        Close(); // 发生错误，关闭会话
    }
}

// --- 仓库选择和消息处理 ---
void Csession::SetActiveRepository(std::shared_ptr<Repository> repo, const std::string& repo_path_for_logging) {
    _active_repository = repo;
    _is_repository_selected.store(repo != nullptr, std::memory_order_release); // memory_order_release用于写入atomic bool
    if (_is_repository_selected.load(std::memory_order_acquire)) {
        _selected_repository_path_for_logging = repo_path_for_logging;
        std::cout << "CSession [" << _uuid << "]: Now serving repository at '" << _selected_repository_path_for_logging << "'" << std::endl;
    } else {
        _selected_repository_path_for_logging.clear();
        std::cout << "CSession [" << _uuid << "]: Active repository cleared." << std::endl;
    }
}

std::shared_ptr<Repository> Csession::GetActiveRepository() const {
    return _active_repository;
}

bool Csession::IsRepositorySelected() const {
    return _is_repository_selected.load(std::memory_order_acquire);
}


void Csession::ProcessTargetRepoRequest(const char* body_data, uint32_t body_length) {
    // 从消息体中提取客户端发送的仓库相对路径字符串
    std::string relative_repo_path_str(body_data, body_length);
    // 移除可能的尾部空字符
    if (!relative_repo_path_str.empty() && relative_repo_path_str.back() == '\0') {
        relative_repo_path_str.pop_back();
    }

    std::cout << "CSession [" << _uuid << "]: Processing MSG_REQ_TARGET_REPO for path: \"" << relative_repo_path_str << "\"" << std::endl;

    // 1. 检查 _server 指针是否有效
    if (!_server) {
        std::cerr << "CSession [" << _uuid << "]: Critical error - CServer instance is null for TARGET_REPO." << std::endl;
        Send("Internal server error (server context missing).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
        Close();
        return;
    }

    // 2. 从CServer获取配置的仓库根目录
    std::filesystem::path repos_root = _server->GetReposRootPath(); // 从CServer获取仓库根目录
    if (repos_root.empty()) {
         std::cerr << "CSession [" << _uuid << "]: Server repositories root path is not configured." << std::endl;
         Send("Server configuration error (repository root not set).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
         return;
    }

    // 3. 对客户端提供的相对路径进行清理和基本的安全检查，防止路径遍历攻击
    std::filesystem::path requested_rel_path_obj(relative_repo_path_str);
    std::filesystem::path final_rel_path; // 用于构建安全的相对路径
    for (const auto& part : requested_rel_path_obj) { // 遍历路径的每个部分
        if (part == "..") { // 禁止路径中使用 ".."
            std::cerr << "CSession [" << _uuid << "]: Invalid repository path (contains '..'): " << relative_repo_path_str << std::endl;
            Send("Invalid repository path (directory traversal attempt).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
            return;
        }
        if (!part.empty() && part != ".") { // 忽略空和 "." 组件
             final_rel_path /= part; // 逐步构建安全的相对路径
        }
    }
    if (final_rel_path.is_absolute()) { // 清理后的路径不应是绝对路径
        std::cerr << "CSession [" << _uuid << "]: Invalid repository path (resolved to absolute after cleaning): " << final_rel_path.string() << std::endl;
        Send("Invalid repository path format (should be relative).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
        return;
    }
     if (final_rel_path.empty() && !relative_repo_path_str.empty() && relative_repo_path_str != ".") {
        std::cerr << "CSession [" << _uuid << "]: Invalid repository path (resolved to empty): " << relative_repo_path_str << std::endl;
        Send("Invalid repository path format (resolved to empty).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
        return;
    }

    // 4. 拼接成服务器本地的完整仓库路径，并进行规范化
    std::filesystem::path full_repo_path = (repos_root / final_rel_path).lexically_normal();

    // 5. 安全检查：确保最终计算出的 full_repo_path 仍然在配置的仓库根目录 repos_root 之下
    auto repos_root_str = repos_root.lexically_normal().generic_string();
    auto full_repo_path_str = full_repo_path.generic_string();
    if (full_repo_path_str.rfind(repos_root_str, 0) != 0) { // 检查full_repo_path_str是否以repos_root_str开头
         std::cerr << "CSession [" << _uuid << "]: Security Alert - Repository path escape attempt: " << full_repo_path_str
                   << " (root: " << repos_root_str << ")" << std::endl;
         Send("Invalid repository path (access denied).", Protocol::MSG_RESP_TARGET_REPO_ERROR);
         return;
    }

    std::cout << "CSession [" << _uuid << "]: Attempting to load repository at: " << full_repo_path.string() << std::endl;

    // 6. 调用 Repository::load 尝试加载指定路径的仓库
    std::optional<Repository> loaded_repo_opt = Repository::load(full_repo_path); //

    if (loaded_repo_opt) {
        try {
            // 创建一个指向加载成功的 Repository 对象的 shared_ptr
            auto shared_repo = std::make_shared<Repository>(std::move(loaded_repo_opt.value()));
            // 调用 SetActiveRepository，将加载的仓库与当前会话关联起来
            SetActiveRepository(shared_repo, final_rel_path.generic_string());
            // 向客户端发送成功确认消息
            Send("Repository selected successfully.", Protocol::MSG_RESP_TARGET_REPO_ACK);
        } catch (const std::bad_optional_access& e) {
            std::string err_msg = "Internal server error: bad_optional_access during repository activation.";
            std::cerr << "CSession [" << _uuid << "]: " << err_msg << " Details: " << e.what() << std::endl;
            Send(err_msg, Protocol::MSG_RESP_TARGET_REPO_ERROR);
            SetActiveRepository(nullptr, ""); // 清理状态
        } catch (const std::exception& e) { // 捕获Repository移动构造等可能抛出的异常
            std::string err_msg = "Internal server error creating shared repository: " + std::string(e.what());
            std::cerr << "CSession [" << _uuid << "]: " << err_msg << std::endl;
            Send(err_msg, Protocol::MSG_RESP_TARGET_REPO_ERROR);
            SetActiveRepository(nullptr, ""); // 清理状态
        }
    } else {
        std::string err_msg = "Error: Failed to load or find repository at specified path: " + final_rel_path.generic_string();
        std::cerr << "CSession [" << _uuid << "]: " << err_msg << std::endl;
        Send(err_msg, Protocol::MSG_RESP_TARGET_REPO_ERROR);
        SetActiveRepository(nullptr, ""); // 确保在失败时也清理状态
    }
}

void Csession::ProcessReceivedData(uint32_t bytes_in_buffer_total) {
    uint32_t consumed_from_current_chunk = 0; // 当前在本次传入的 bytes_in_buffer_total 中已处理的字节数
    const char* current_chunk_data_ptr = _recv_buffer.data(); // 指向本次要处理的数据块的开头

    // 只要当前数据块 (_recv_buffer 中的 bytes_in_buffer_total) 还有未处理的部分
    while (consumed_from_current_chunk < bytes_in_buffer_total) {
        if (_is_parsing_header) { // --- 阶段1: 正在尝试解析消息头 ---
            // 计算还需要多少字节才能构成一个完整的头部
            uint32_t needed_for_header = Protocol::HEAD_TOTAL_LEN - _header_buffer_current_len;
            // 本次数据块中可用于填充头部的数据量
            uint32_t available_in_chunk_for_header = bytes_in_buffer_total - consumed_from_current_chunk;
            // 实际能从当前数据块拷贝到头部缓冲区的字节数
            uint32_t to_copy_for_header = std::min(needed_for_header, available_in_chunk_for_header);

            // 从当前数据块拷贝数据到 _header_buffer
            std::memcpy(_header_buffer + _header_buffer_current_len,
                        current_chunk_data_ptr + consumed_from_current_chunk,
                        to_copy_for_header);
            _header_buffer_current_len += static_cast<uint16_t>(to_copy_for_header); // 更新已接收的头部长度
            consumed_from_current_chunk += to_copy_for_header; // 更新在当前数据块中已消耗的字节数

            if (_header_buffer_current_len == Protocol::HEAD_TOTAL_LEN) { // 头部已完整接收
                uint16_t msg_id_parsed;
                uint32_t body_length_parsed;
                // 解包头部
                if (Protocol::unpack_header(_header_buffer, msg_id_parsed, body_length_parsed)) {
                    // std::cout << "CSession [" << _uuid << "]: Header parsed. ID=" << msg_id_parsed << ", BodyLen=" << body_length_parsed << std::endl;

                    // 创建新的 RecvNode 来存储消息信息
                    _current_processing_node = std::make_shared<RecvNode>(msg_id_parsed, body_length_parsed);
                    _is_parsing_header = false;         // 切换到解析消息体状态
                    _header_buffer_current_len = 0;     // 重置头部缓冲区，为下一个消息做准备

                    if (body_length_parsed == 0) { // 如果消息体长度为0
                        // 这是一个完整的、没有消息体的消息
                        // 检查是否是 MSG_REQ_TARGET_REPO (按协议它不应为空)
                        if (msg_id_parsed == Protocol::MSG_REQ_TARGET_REPO) {
                            std::cerr << "CSession [" << _uuid << "]: MSG_REQ_TARGET_REPO received with empty body, invalid." << std::endl;
                            Send("TARGET_REPO request requires a non-empty path in body.", Protocol::MSG_RESP_TARGET_REPO_ERROR);
                        }
                        // 检查是否是注册或登录请求 (按协议它们也不应为空)
                        else if (msg_id_parsed == Protocol::MSG_REQ_REGISTER_USER || msg_id_parsed == Protocol::MSG_REQ_LOGIN_USER) {
                            std::cerr << "CSession [" << _uuid << "]: Auth request (ID " << msg_id_parsed << ") received with empty body, invalid." << std::endl;
                            Send("Register/Login request requires username and password.", Protocol::MSG_RESP_ERROR);
                        }
                        // MSG_REQ_LIST_REFS 现在需要 Token，所以 body_length 不会是0
                        else if (msg_id_parsed == Protocol::MSG_TEST_PING_REQ) { // 例如 PING 请求
                            PostToLogicSystem(_current_processing_node); // 直接投递
                        } else {
                            // 其他未明确定义为空消息体但收到空消息体的请求
                            std::cerr << "CSession [" << _uuid << "]: Received msg ID " << msg_id_parsed << " with zero body length, but not recognized as a valid no-body request." << std::endl;
                            Send("Invalid request: unexpected empty body for message ID " + std::to_string(msg_id_parsed) + ".", Protocol::MSG_RESP_ERROR);
                        }
                        _current_processing_node.reset(); // 清理当前处理节点
                        _is_parsing_header = true;        // 切换回解析头部状态
                        // 继续 while 循环的下一次迭代，处理 buffer 中可能存在的下一个消息
                    }
                    // 如果 body_length_parsed > 0，则本轮 if 结束，将进入下面的消息体处理逻辑
                } else { // 头部解包失败
                    std::cerr << "CSession [" << _uuid << "]: Error unpacking header. Invalid header format. Closing session." << std::endl;
                    Close();
                    return;
                }
            } else { // 头部数据还未收全，并且当前数据块已全部用于填充头部
                // (consumed_from_current_chunk == bytes_in_buffer_total 隐含于此)
                break; // 跳出 while 循环，等待下一次 HandleRead
            }
        } // end if (_is_parsing_header)

        // --- 阶段2: 正在尝试解析消息体 (仅当头部已成功解析且消息体长度 > 0) ---
        if (!_is_parsing_header && _current_processing_node) { // 确保有一个待处理的消息节点
            // 如果消息体的预期长度为0，应该在解析头部后就处理掉了。
            // 如果由于某种原因（例如上一个消息恰好填满 TCP 包，而当前消息头部指示 body_length > 0）
            // 再次进入这里，但 _current_processing_node 的消息体实际上已经完成了（例如，它的 _expected_body_length 就是0），
            // 这可能表明一个状态同步问题或逻辑流程缺陷。
            // RecvNode::is_body_complete() 应该在其 _expected_body_length 为0时立即返回 true。
            if (_current_processing_node->is_body_complete()) {
                // 理论上，如果 is_body_complete() 为 true，说明 expected_body_length 为 0，
                // 这应该在上面 body_length_parsed == 0 的分支中处理过了。
                // 如果不是，那么这是一个意外状态。
                // std::cout << "CSession [" << _uuid << "]: Warning - Entered body parsing, but node body is already complete (expected_len="
                //           << _current_processing_node->get_expected_body_length() <<"). Resetting to parse header." << std::endl;
                _is_parsing_header = true;
                _current_processing_node.reset(); // 清理节点，避免重复处理
                continue; // 返回 while 循环顶部，尝试解析下一个消息的头部
            }

            // 计算当前数据块中还剩多少数据可用于填充消息体
            uint32_t available_in_chunk_for_body = bytes_in_buffer_total - consumed_from_current_chunk;
            if (available_in_chunk_for_body == 0) { // 如果当前数据块的数据已全部消耗完毕
                break; // 跳出 while 循环，等待下一次 HandleRead
            }

            // 将数据从当前数据块追加到当前消息节点的 body_data 中
            uint32_t copied_to_body = _current_processing_node->append_data(
                                        current_chunk_data_ptr + consumed_from_current_chunk,
                                        available_in_chunk_for_body);
            consumed_from_current_chunk += copied_to_body; // 更新在当前数据块中已消耗的字节数

            if (_current_processing_node->is_body_complete()) { // 消息体已完整接收
                uint16_t current_msg_id = _current_processing_node->get_msg_id();

                // 特殊处理 MSG_REQ_TARGET_REPO，它由 CSession 直接处理
                if (current_msg_id == Protocol::MSG_REQ_TARGET_REPO) {
                    ProcessTargetRepoRequest(_current_processing_node->get_body_data(),
                                             _current_processing_node->get_current_body_length());
                } else {
                    // 对于其他所有消息，投递给 LogicSystem 前进行仓库上下文检查
                    bool needs_repo_context = true; // 默认所有非TARGET_REPO的请求都需要仓库上下文

                    // 定义那些不需要预先选定仓库即可处理的客户端请求ID
                    if (current_msg_id == Protocol::MSG_TEST_ECHO_REQ ||
                        current_msg_id == Protocol::MSG_TEST_PING_REQ ||
                        current_msg_id == Protocol::MSG_REQ_REGISTER_USER || // 注册用户不需要选定仓库
                        current_msg_id == Protocol::MSG_REQ_LOGIN_USER      // 登录用户不需要选定仓库
                        // 添加其他任何不需要仓库上下文的请求ID (例如，全局配置获取等，如果未来有的话)
                        ) {
                        needs_repo_context = false;
                    }

                    // 服务器不应该处理来自客户端的“服务器响应”类型的消息
                    // (简单地假设响应ID都在一个较高范围)
                    if (current_msg_id >= Protocol::MSG_RESP_ACK_OK && current_msg_id <= Protocol::MSG_RESP_AUTH_REQUIRED) { // 使用协议中定义的响应范围
                        needs_repo_context = false; // 不适用仓库上下文检查
                        std::cerr << "CSession [" << _uuid << "]: Warning - Received a message with a server-response-type ID ("
                                  << current_msg_id << ") from client. Discarding." << std::endl;
                        Send("Invalid message ID received from client (server response type).", Protocol::MSG_RESP_ERROR);
                        // 不投递给 LogicSystem，直接准备处理下一个消息
                    } else if (needs_repo_context && !IsRepositorySelected()) {
                        // 如果消息需要仓库上下文，但当前会话没有选定仓库
                         std::cerr << "CSession [" << _uuid << "]: Received msg ID " << current_msg_id
                                  << " which requires a repository, but none selected." << std::endl;
                         Send("No repository selected for this operation. Please use TARGET_REPO command first.", Protocol::MSG_RESP_ERROR);
                    } else {
                        // 仓库已选定，或者消息类型不需要选定仓库 (如注册、登录、测试)
                        PostToLogicSystem(_current_processing_node); // 投递给 LogicSystem
                    }
                }
                _current_processing_node.reset(); // 清理当前已处理的消息节点
                _is_parsing_header = true;        // 切换回解析头部状态，为下一个消息做准备
            } else if (consumed_from_current_chunk == bytes_in_buffer_total) {
                // 消息体还未完整，但当前数据块的数据已全部用于填充当前消息体
                break; // 跳出 while 循环，等待下一次 HandleRead 带来更多数据
            }
            // 如果 copied_to_body < available_in_chunk_for_body，理论上不应发生，
            // 因为这意味着 append_data 没有消耗所有可用的数据，但消息体又未完成。
            // 这可能指示 RecvNode::append_data 的逻辑或容量问题。
            // 但如果消息体刚好完成，并且 available_in_chunk_for_body 还有剩余（属于下一个消息），
            // consumed_from_current_chunk 会小于 bytes_in_buffer_total，while 循环会继续。
        } // end if (!_is_parsing_header && _current_processing_node)
    } // end while (consumed_from_current_chunk < bytes_in_buffer_total)

}

void Csession::HandleRead(const boost::system::error_code& error, size_t bytes_transferred_size_t) {
    if (_is_closed.load(std::memory_order_acquire)) { // 先检查会话是否已关闭
        return;
    }

    if (!error) { // 如果异步读取操作没有错误
        if (bytes_transferred_size_t == 0) {
            // 收到0字节通常表示对方已正常关闭连接 (发送了FIN)
            std::cout << "CSession [" << _uuid << "]: Connection closed by peer (received 0 bytes)." << std::endl;
            Close(); // 关闭我方会话
            return;
        }
        // std::cout << "CSession [" << _uuid << "]: HandleRead received " << bytes_transferred_size_t << " bytes." << std::endl;
        ProcessReceivedData(static_cast<uint32_t>(bytes_transferred_size_t)); // 处理接收到的数据

        if (!_is_closed.load(std::memory_order_acquire)) { // 如果在ProcessReceivedData中没有关闭会话
            AsyncRead(); // 继续下一次异步读取
        }
    } else { // 如果异步读取操作发生错误
        if (error == boost::asio::error::eof) {
            std::cout << "CSession [" << _uuid << "]: Connection closed by peer (EOF)." << std::endl;
        } else if (error == boost::asio::error::operation_aborted) {
            // std::cout << "CSession [" << _uuid << "]: Read operation aborted (session likely closing)." << std::endl;
        } else {
            std::cerr << "CSession [" << _uuid << "]: HandleRead error: " << error.message() << std::endl;
        }
        Close(); // 发生错误，关闭会话
    }
}

void Csession::PostToLogicSystem(std::shared_ptr<RecvNode> complete_node) {
    if (!complete_node || !complete_node->is_body_complete()) { // 防御性检查
        // std::cerr << "CSession [" << _uuid << "]: Attempt to post incomplete or null RecvNode to LogicSystem." << std::endl;
        return;
    }
    // std::cout << "CSession [" << _uuid << "]: Posting msg ID " << complete_node->get_msg_id() << " to LogicSystem." << std::endl;
    auto logic_node = std::make_shared<LogicNode>(SharedSelf(), complete_node);
    _logic_system.PostMsgToQue(logic_node); // 将 LogicNode 投递给 LogicSystem
    // 注意：_current_processing_node 的 reset 应该在调用 PostToLogicSystem 之后，
    // 或者在 ProcessReceivedData 中消息体处理完毕、准备解析下一个头部时进行。
    // 在这里的版本，_current_processing_node 在 ProcessReceivedData 中被 reset。
}


}
