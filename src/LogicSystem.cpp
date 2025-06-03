#include "../include/LogicSystem.h"
#include <iostream>

#include "Csession.h"
#include"protocol.h"
#include "Repository.h"
#include "UserManager.h"


namespace Biogit {

LogicSystem::LogicSystem() : _b_stop(false) {
    std::cout << "LogicSystem: Instance created." << std::endl;
}

LogicSystem::~LogicSystem() {
    std::cout << "LogicSystem: Destructing..." << std::endl;
    StopService(); // 确保在析构时服务已停止，工作线程已汇合
    std::cout << "LogicSystem: Destructed." << std::endl;
}

bool LogicSystem::Start() {
    if (_worker_thread.joinable()) { // 防止重复启动线程
        std::cout << "LogicSystem: Worker thread is already running." << std::endl;
        return true;
    }
    _b_stop.store(false, std::memory_order_release); // 重置停止标志
    RegisterCallBacks(); // 注册所有消息处理函数
    _worker_thread = std::thread(&LogicSystem::DealMsg, this); // 创建并启动工作线程
    std::cout << "LogicSystem: Worker thread started." << std::endl;
    return true;
}

void LogicSystem::StopService() {
    // 原子操作，确保停止逻辑只被有效执行一次
    if (!_b_stop.exchange(true, std::memory_order_acq_rel)) {
        std::cout << "LogicSystem: Stopping service..." << std::endl;
        _consume.notify_one(); // 唤醒可能因队列为空而等待的工作线程
        if (_worker_thread.joinable()) {
            _worker_thread.join(); // 等待工作线程执行完毕并安全退出
        }
        std::cout << "LogicSystem: Service stopped." << std::endl;
    }
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    if (_b_stop.load(std::memory_order_acquire)) { // 如果服务已停止，不再接受新消息
        std::cout << "LogicSystem: Service is stopped, not accepting new messages." << std::endl;
        return;
    }
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _msg_que.push(msg);
    }
    _consume.notify_one();
}

void LogicSystem::DealMsg() {
    std::cout << "LogicSystem: DealMsg thread entering loop." << std::endl;
    while (true) { // 工作线程主循环
        std::shared_ptr<LogicNode> logic_node; // 用于存储从队列中取出的消息节点
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _consume.wait(lock, [this] { return _b_stop.load(std::memory_order_relaxed) || !_msg_que.empty(); });// 等待条件：或者服务被标记为停止，或者消息队列不为空

            // 如果服务已停止并且消息队列也已空，则退出循环
            if (_b_stop.load(std::memory_order_relaxed) && _msg_que.empty()) {
                std::cout << "LogicSystem: Stopping signal received and message queue empty, exiting DealMsg loop." << std::endl;
                break;
            }

            // 如果队列中还有消息，则应继续处理完队列中的消息
            if (_msg_que.empty()){
                if(_b_stop.load(std::memory_order_relaxed)) continue; // 继续循环，下一次会检查退出条件
            }

            logic_node = _msg_que.front();
            _msg_que.pop();
        }

        // 检查取出的节点是否有效
        if (logic_node && logic_node->_session && logic_node->_recv_node) {
            std::cout << "LogicSystem: Processing msg ID " << logic_node->_recv_node->get_msg_id() << " from session [" << logic_node->_session->GetUuid() << "]" << std::endl;

            // 根据消息ID查找对应的回调函数
            auto it = _fun_callbacks.find(logic_node->_recv_node->get_msg_id());
            if (it != _fun_callbacks.end()) { // 如果找到了回调函数
                try {
                    // 执行回调函数，传入会话指针、消息ID、消息体数据指针和长度
                    it->second(logic_node->_session,
                               logic_node->_recv_node->get_msg_id(),
                               logic_node->_recv_node->get_body_data(),
                               logic_node->_recv_node->get_current_body_length());
                } catch (const std::exception& e) { // 捕获回调函数中可能抛出的异常
                    std::cerr << "LogicSystem: Exception caught in callback for msg ID "
                              << logic_node->_recv_node->get_msg_id() << " from session ["
                              << logic_node->_session->GetUuid() << "]: " << e.what() << std::endl;
                    // 发生内部错误，向客户端发送一个通用的错误响应
                    if(logic_node->_session && !logic_node->_session->IsClosed()){
                        std::string error_message = "Server internal error while processing request.";
                        logic_node->_session->Send(error_message, Protocol::MSG_RESP_ERROR);
                    }
                }
            } else { // 如果没有为该消息ID注册回调函数
                std::cerr << "LogicSystem: No callback registered for msg ID "
                          << logic_node->_recv_node->get_msg_id()
                          << " from session [" << logic_node->_session->GetUuid() << "]." << std::endl;
                 if(logic_node->_session && !logic_node->_session->IsClosed()){
                    std::string error_message = "Unknown or unsupported request ID.";
                    logic_node->_session->Send(error_message, Protocol::MSG_RESP_ERROR);
                 }
            }
        } else if (logic_node) { // logic_node有效，但内部的session或recv_node为空
            std::cerr << "LogicSystem: Received a LogicNode with null internal session or recv_node." << std::endl;
        }
    }
    std::cout << "LogicSystem: DealMsg thread finished." << std::endl;
}

void LogicSystem::RegisterCallBacks() {
    _fun_callbacks[Protocol::MSG_REQ_LIST_REFS] = std::bind(&LogicSystem::HandleReqListRefs, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_GET_OBJECT] = std::bind(&LogicSystem::HandleReqGetObject, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_CHECK_OBJECTS] = std::bind(&LogicSystem::HandleReqCheckObjects, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_PUT_OBJECT] = std::bind(&LogicSystem::HandleReqPutObject, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_UPDATE_REF] = std::bind(&LogicSystem::HandleReqUpdateRef, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_REGISTER_USER] = std::bind(&LogicSystem::HandleReqRegisterUser, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _fun_callbacks[Protocol::MSG_REQ_LOGIN_USER] = std::bind(&LogicSystem::HandleReqLoginUser, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

    std::cout << "LogicSystem: Callbacks registered (including auth)." << std::endl;
}

// --- Token 处理的辅助函数 ---
/**
 * @brief 从包含Token前缀的完整消息体中提取Token字符串和原始的业务数据载荷。
 * @details 协议约定: 消息体格式为 <token_str_with_null_term>\0<original_defined_message_body>
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 * @return 一个 std::tuple，包含三个元素：
 * 1. std::string: 提取出的Token字符串 (不包含其末尾的 '\0')。如果未找到Token或格式错误，则为空字符串。
 * 2. const char*: 指向原始业务数据载荷起始位置的指针。如果Token无效或原始载荷为空，则为 nullptr 或指向分隔符'\0'之后的位置。
 * 3. uint32_t: 原始业务数据载荷的长度。如果Token无效或原始载荷为空，则为 0。
 */
std::tuple<std::string, const char*, uint32_t> LogicSystem::extractTokenAndPayload(
    const char* body_data_with_token, uint32_t body_length_with_token) {

    std::string token_str;
    const char* original_payload_ptr = nullptr;
    uint32_t original_payload_len = 0;

    if (body_data_with_token == nullptr || body_length_with_token == 0) {
        return {token_str, original_payload_ptr, original_payload_len};
    }

    // 查找第一个 '\0'，它标记 Token 字符串的结束
    const char* first_null_terminator = static_cast<const char*>(memchr(body_data_with_token, '\0', body_length_with_token));

    if (first_null_terminator != nullptr) {
        size_t token_length = first_null_terminator - body_data_with_token;
        token_str.assign(body_data_with_token, token_length);

        size_t separator_and_token_length = token_length + 1;

        if (separator_and_token_length < body_length_with_token) {
            original_payload_ptr = body_data_with_token + separator_and_token_length;
            original_payload_len = body_length_with_token - separator_and_token_length;
        } else {
            original_payload_ptr = body_data_with_token + separator_and_token_length;
            original_payload_len = 0;
        }
    }
    return {token_str, original_payload_ptr, original_payload_len};
}


/**
 * @brief 对指定请求进行认证并准备其原始业务数据载荷。
 * @details
 *   首先调用 extractTokenAndPayload 从传入的完整消息体中分离出 Token 和原始载荷 \n
 *   然后，使用 TokenManager 验证 Token 的有效性（包括签名和有效期）。
 * @param session 当前客户端会话的共享指针，用于发送认证失败的响应。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 * @param handler_name_for_logging 用于日志记录的当前处理程序或操作的名称。
 * @param out_original_body_ptr (输出参数) 如果认证成功，此指针将指向原始业务数据载荷的起始位置。
 * @param out_original_body_len (输出参数) 如果认证成功，此处将存储原始业务数据载荷的长度。
 * @param out_username_from_token (输出参数) 如果认证成功，此处将存储从有效Token中解析出的用户名。
 * @return 如果 Token 有效且认证成功，则返回 true，并且输出参数会被正确填充。
 * 如果 Token 无效、缺失或过期，则向客户端发送 MSG_RESP_AUTH_REQUIRED 响应，并返回 false。
 */
bool LogicSystem::authenticateAndPreparePayload(
    std::shared_ptr<Csession> session,
    const char* body_data_with_token,
    uint32_t body_length_with_token,
    const std::string& handler_name_for_logging,
    const char*& out_original_body_ptr,
    uint32_t& out_original_body_len,
    std::string& out_username_from_token)
{
    out_original_body_ptr = nullptr;
    out_original_body_len = 0;
    out_username_from_token.clear();

    auto [token_str, original_body_ptr_internal, original_body_len_internal] =
        extractTokenAndPayload(body_data_with_token, body_length_with_token);

    if (token_str.empty() || !TokenManager::GetInstance()->validateToken(token_str, out_username_from_token)) {
        std::cerr << "LogicSystem: Auth failed or missing token for " << handler_name_for_logging
                  << " from session [" << session->GetUuid() << "]. Token received: '" << token_str << "'" << std::endl;
        session->Send("Authentication required or token invalid.", Protocol::MSG_RESP_AUTH_REQUIRED);
        return false;
    }

    out_original_body_ptr = original_body_ptr_internal;
    out_original_body_len = original_body_len_internal;

    std::cout << "LogicSystem: User '" << out_username_from_token << "' authenticated for " << handler_name_for_logging
              << ". Session [" << session->GetUuid() << "]" << std::endl;
    return true;
}

// --- biogit2 协议处理回调函数实现 ---
/**
 * @brief 处理客户端发送的 MSG_REQ_REGISTER_USER (用户注册) 请求。
 * 1. 从消息体中解析出客户端尝试注册的用户名和密码。
 * - 协议约定消息体格式为: <username_str_with_null_term><password_str_with_null_term>
 * - 例如: "newuser\0securepass123\0"
 * 2. 对解析出的用户名和密码进行基本校验
 * 3. 调用 UserManager 的 registerUser 方法尝试注册新用户。 UserManager 会处理用户名是否已存在、密码加盐哈希及存储等逻辑。
 * 4. 根据 UserManager::registerUser 的结果，向客户端会话发送相应的响应：
 * - Protocol::MSG_RESP_REGISTER_SUCCESS (注册成功)
 * - Protocol::MSG_RESP_REGISTER_FAILURE (注册失败，响应体中可能包含错误信息)
 * @param session 指向发起此请求的 CSession 对象的共享指针。
 * @param msg_id 收到的消息ID (应为 Protocol::MSG_REQ_REGISTER_USER)。
 * @param body_data 指向消息体数据的指针，包含用户名和密码。
 * @param body_length 消息体的总长度。
 */
void LogicSystem::HandleReqRegisterUser(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length) {
    if (!session || session->IsClosed()) return;

    const char* ptr = body_data;
    size_t remaining_len = body_length;

    // 1. 解析用户名
    const char* username_end = static_cast<const char*>(memchr(ptr, '\0', remaining_len));
    if (!username_end || username_end == ptr) {
        session->Send("Register failed: Username missing or empty.", Protocol::MSG_RESP_REGISTER_FAILURE);
        return;
    }
    std::string username(ptr, username_end - ptr);
    size_t username_len_with_null = (username_end - ptr) + 1;

    if (username_len_with_null >= remaining_len) {
        session->Send("Register failed: Password missing.", Protocol::MSG_RESP_REGISTER_FAILURE);
        return;
    }
    ptr += username_len_with_null;
    remaining_len -= username_len_with_null;

    // 2. 解析密码
    const char* password_end = static_cast<const char*>(memchr(ptr, '\0', remaining_len));
    if (!password_end || password_end == ptr) {
        session->Send("Register failed: Password missing or empty.", Protocol::MSG_RESP_REGISTER_FAILURE);
        return;
    }
    std::string password(ptr, password_end - ptr);
    size_t password_len_with_null = (password_end - ptr) + 1;
    if (username_len_with_null + password_len_with_null > body_length) {
        session->Send("Register failed: Malformed payload.", Protocol::MSG_RESP_REGISTER_FAILURE);
        return;
    }

    std::cout << "LogicSystem: Processing MSG_REQ_REGISTER_USER for user '" << username << "' from session [" << session->GetUuid() << "]" << std::endl;

    // 3. 调用 UserManager 进行用户注册
    std::string error_message;
    if (UserManager::GetInstance()->registerUser(username, password, error_message)) {
        session->Send(error_message, Protocol::MSG_RESP_REGISTER_SUCCESS);
    } else {
        session->Send(error_message, Protocol::MSG_RESP_REGISTER_FAILURE);
    }
}

/**
 * @brief 处理客户端发送的 MSG_REQ_LOGIN_USER (用户登录) 请求。
 * 1. 从消息体中解析出客户端尝试登录的用户名和密码。
 * - 协议约定消息体格式为: <username_str_with_null_term><password_str_with_null_term>
 * 2. 调用 UserManager 的 verifyCredentials 方法验证用户凭证。
 * - UserManager 会处理用户查找、盐值获取、密码哈希比较等逻辑。
 * 3. 如果凭证验证成功：
 * a. 调用 TokenManager 的 generateToken 方法为该用户生成一个认证 Token。
 * b. 将生成的 Token 通过 MSG_RESP_LOGIN_SUCCESS 消息发送给客户端。
 * - 协议约定响应体格式为: <token_str_with_null_term>
 * 4. 如果凭证验证失败或Token生成失败，向客户端发送 MSG_RESP_LOGIN_FAILURE 响应(响应体中可能包含错误信息)。
 * @param session 指向发起此请求的 CSession 对象的共享指针。
 * @param msg_id 收到的消息ID (应为 Protocol::MSG_REQ_LOGIN_USER)。
 * @param body_data 指向消息体数据的指针，包含用户名和密码。
 * @param body_length 消息体的总长度。
 */
void LogicSystem::HandleReqLoginUser(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length) {
    if (!session || session->IsClosed()) return;

    // 1. 解析用户名和密码
    const char* ptr = body_data;
    size_t remaining_len = body_length;
    const char* username_end = static_cast<const char*>(memchr(ptr, '\0', remaining_len));
    if (!username_end || username_end == ptr) { session->Send("Login failed: Invalid payload (username).", Protocol::MSG_RESP_LOGIN_FAILURE); return; }
    std::string username(ptr, username_end - ptr);
    size_t username_len_with_null = (username_end - ptr) + 1;
    if (username_len_with_null >= remaining_len) { session->Send("Login failed: Invalid payload (password missing).", Protocol::MSG_RESP_LOGIN_FAILURE); return; }
    ptr += username_len_with_null;
    remaining_len -= username_len_with_null;
    const char* password_end = static_cast<const char*>(memchr(ptr, '\0', remaining_len));
    if (!password_end || password_end == ptr) { session->Send("Login failed: Invalid payload (password format).", Protocol::MSG_RESP_LOGIN_FAILURE); return; }
    std::string password(ptr, password_end - ptr);
    size_t password_len_with_null = (password_end - ptr) + 1;
     if (username_len_with_null + password_len_with_null > body_length) {
         session->Send("Login failed: Malformed payload (length).", Protocol::MSG_RESP_LOGIN_FAILURE);
        return;
    }

    std::cout << "LogicSystem: Processing MSG_REQ_LOGIN_USER for user '" << username << "' from session [" << session->GetUuid() << "]" << std::endl;

    // 2. 调用 UserManager 验证用户凭证
    std::string error_message;
    if (UserManager::GetInstance()->verifyCredentials(username, password, error_message)) {
        std::string token = TokenManager::GetInstance()->generateToken(username);
        if (!token.empty()) {
            std::string token_with_null = token;
            token_with_null.push_back('\0');
            session->Send(token_with_null, Protocol::MSG_RESP_LOGIN_SUCCESS); //
        } else {
            session->Send("Login failed: Could not generate token.", Protocol::MSG_RESP_LOGIN_FAILURE); //
        }
    } else {
        session->Send(error_message, Protocol::MSG_RESP_LOGIN_FAILURE); //
    }
}


/**
 * @brief 处理客户端发送的 MSG_REQ_LIST_REFS (列出引用) 请求。
 * 1. 对客户端进行认证：从消息体中提取Token，并使用 TokenManager 进行验证。
 * - 如果认证失败，向客户端发送 MSG_RESP_AUTH_REQUIRED 并返回。
 * - 协议约定消息体格式为: <token_str_with_null_term>\0 (原始消息体为空)
 * 2. 检查当前会话是否已选定一个有效的仓库。
 * - 如果未选定或仓库无效，发送 MSG_RESP_ERROR 并返回。
 * 3. 调用当前活动仓库的 get_all_local_refs 方法获取所有本地引用。
 * 4. 将引用列表格式化并通过一系列消息发送给客户端：
 * - 首先发送 MSG_RESP_REFS_LIST_BEGIN。
 * - 对每个引用，发送一条 MSG_RESP_REFS_ENTRY 消息。
 * - 协议约定 MSG_RESP_REFS_ENTRY 消息体格式为: <ref_name_str_with_null_term><ref_value_str_with_null_term>
 * - 最后发送 MSG_RESP_REFS_LIST_END。
 * 5. 处理过程中发生的任何异常将导致向客户端发送 MSG_RESP_ERROR。
 * @param session 指向发起此请求的 CSession 对象的共享指针。
 * @param msg_id 收到的消息ID (应为 Protocol::MSG_REQ_LIST_REFS)。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 */
void LogicSystem::HandleReqListRefs(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data_with_token, uint32_t body_length_with_token) {
    if (!session || session->IsClosed()) return;

    const char* original_body_ptr = nullptr;
    uint32_t original_body_len = 0;
    std::string username_from_token;

    // 1. 进行认证并准备载荷 (对于LIST_REFS，原始载荷为空)
    if (!authenticateAndPreparePayload(session, body_data_with_token, body_length_with_token, "LIST_REFS", original_body_ptr, original_body_len, username_from_token)) {
        return;
    }

    // 2. 检查仓库是否已选定
    if (!session->IsRepositorySelected()) {
        session->Send("No repository selected for LIST_REFS.", Protocol::MSG_RESP_ERROR); return;
    }
    std::shared_ptr<Repository> active_repo = session->GetActiveRepository();
    if (!active_repo) {
        session->Send("Server internal error: active repository context lost for LIST_REFS.", Protocol::MSG_RESP_ERROR); return;
    }

    // 3. 获取所有本地引用
    std::vector<std::pair<std::string, std::string>> refs;
    try {
        refs = active_repo->get_all_local_refs(); //
    } catch (const std::exception& e) {
        std::cerr << "LogicSystem: Exception from get_all_local_refs: " << e.what() << std::endl;
        session->Send("Server error: failed to retrieve repository references.", Protocol::MSG_RESP_ERROR); return;
    }

    // 4. 发送引用列表给客户端
    session->Send("", 0, Protocol::MSG_RESP_REFS_LIST_BEGIN); //
    for (const auto& ref_pair : refs) {
        std::string payload_str = ref_pair.first;
        payload_str.push_back('\0');
        payload_str.append(ref_pair.second);
        payload_str.push_back('\0');
        session->Send(payload_str, Protocol::MSG_RESP_REFS_ENTRY); //
    }
    session->Send("", 0, Protocol::MSG_RESP_REFS_LIST_END); //
}


/**
 * @brief 处理客户端发送的 MSG_REQ_GET_OBJECT (获取对象) 请求。
 * 1. 认证客户端：从消息体提取并验证Token。
 * - 协议约定消息体格式为: <token_str_with_null_term>\0<40_char_sha1_hash_string>
 * 2. 检查仓库是否选定。
 * 3. 从原始载荷中解析出40字节的对象哈希字符串。
 * 4. 调用活动仓库的 get_raw_object_content 方法获取对象的原始内容。
 * 5. 根据获取结果：
 * - 如果找到对象，发送 MSG_RESP_OBJECT_CONTENT，消息体包含对象哈希和对象的原始内容。
 * - 如果未找到对象，发送 MSG_RESP_OBJECT_NOT_FOUND，消息体包含被请求的对象哈希。
 * - 如果发生其他错误，发送 MSG_RESP_ERROR。
 * @param session 指向 CSession 的共享指针。
 * @param msg_id 消息ID (应为 Protocol::MSG_REQ_GET_OBJECT)。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 */
void LogicSystem::HandleReqGetObject(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data_with_token, uint32_t body_length_with_token) {
    if (!session || session->IsClosed()) return;

    const char* original_body_ptr = nullptr;
    uint32_t original_body_len = 0;
    std::string username_from_token;

    // 1. 认证并准备载荷
    if (!authenticateAndPreparePayload(session, body_data_with_token, body_length_with_token, "GET_OBJECT", original_body_ptr, original_body_len, username_from_token)) {
        return;
    }

    // 2. 校验原始载荷的格式 (应该是40字节的哈希)
    if (original_body_len != 40 || original_body_ptr == nullptr) {
        session->Send("Invalid request payload for GET_OBJECT (expected 40-byte hash).", Protocol::MSG_RESP_ERROR); return;
    }
    std::string object_hash(original_body_ptr, original_body_len);

    // 3. 检查仓库是否选定
    if (!session->IsRepositorySelected()) { session->Send("No repository selected for GET_OBJECT.", Protocol::MSG_RESP_ERROR); return; }
    std::shared_ptr<Repository> active_repo = session->GetActiveRepository();
    if (!active_repo) { session->Send("Server internal error: repo context lost for GET_OBJECT.", Protocol::MSG_RESP_ERROR); return; }

    // 4. 获取对象内容
    std::optional<std::vector<char>> raw_content_opt = active_repo->get_raw_object_content(object_hash); //

    // 5. 发送响应
    if (raw_content_opt && !raw_content_opt->empty()) {
        std::vector<char> response_payload_go;
        response_payload_go.reserve(40 + raw_content_opt->size());
        response_payload_go.insert(response_payload_go.end(), object_hash.begin(), object_hash.end());
        response_payload_go.insert(response_payload_go.end(), raw_content_opt->begin(), raw_content_opt->end());
        session->Send(response_payload_go, Protocol::MSG_RESP_OBJECT_CONTENT); //
    } else {
        session->Send(object_hash, Protocol::MSG_RESP_OBJECT_NOT_FOUND); //
    }
}


/**
 * @brief 处理客户端发送的 MSG_REQ_CHECK_OBJECTS (检查对象是否存在) 请求。
 * 1. 认证客户端。
 * - 协议约定消息体格式为: <token_str_with_null_term>\0<num_hashes_uint32_t_net><40_char_sha1_1>...
 * 2. 检查仓库是否选定。
 * 3. 从原始载荷中解析出对象哈希的数量和各个哈希字符串。
 * 4. 对每个哈希，调用活动仓库的 object_exists 方法检查对象是否存在。
 * 5. 构造并发送 MSG_RESP_CHECK_OBJECTS_RESULT 响应，消息体包含结果数量和每个哈希的存在状态字节列表。
 * @param session 指向 CSession 的共享指针。
 * @param msg_id 消息ID (应为 Protocol::MSG_REQ_CHECK_OBJECTS)。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 */
void LogicSystem::HandleReqCheckObjects(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data_with_token, uint32_t body_length_with_token) {
    if (!session || session->IsClosed()) return;

    const char* original_body_ptr = nullptr;
    uint32_t original_body_len = 0;
    std::string username_from_token;

    // 1. 认证并准备载荷
    if (!authenticateAndPreparePayload(session, body_data_with_token, body_length_with_token, "CHECK_OBJECTS", original_body_ptr, original_body_len, username_from_token)) {
        return;
    }

    // 2. 检查仓库是否选定
    if (!session->IsRepositorySelected()) { session->Send("No repository selected for CHECK_OBJECTS.", Protocol::MSG_RESP_ERROR); return; }
    std::shared_ptr<Repository> active_repo = session->GetActiveRepository();
    if (!active_repo) { session->Send("Server internal error: repo context lost for CHECK_OBJECTS.", Protocol::MSG_RESP_ERROR); return; }

    // 3. 解析原始载荷: <num_hashes_uint32_t_net><40_char_sha1_1><40_char_sha1_2>...
    if (original_body_len < sizeof(uint32_t)) {
        session->Send("Invalid payload for CHECK_OBJECTS (too short for count).", Protocol::MSG_RESP_ERROR); return;
    }
    uint32_t num_hashes_net;
    std::memcpy(&num_hashes_net, original_body_ptr, sizeof(uint32_t));
    uint32_t num_hashes = boost::asio::detail::socket_ops::network_to_host_long(num_hashes_net);

    // 校验消息体总长度是否与哈希数量和哈希长度匹配
    if (original_body_len != sizeof(uint32_t) + num_hashes * 40) {
        session->Send("Payload length mismatch for CHECK_OBJECTS.", Protocol::MSG_RESP_ERROR); return;
    }

    // 4. 检查每个哈希是否存在
    std::vector<char> results_status_bytes;
    results_status_bytes.reserve(num_hashes);
    const char* current_hash_ptr_orig = original_body_ptr + sizeof(uint32_t);
    for (uint32_t i = 0; i < num_hashes; ++i) {
        std::string hash_to_check(current_hash_ptr_orig, 40);
        results_status_bytes.push_back(active_repo->object_exists(hash_to_check) ? static_cast<char>(0x01) : static_cast<char>(0x00)); //
        current_hash_ptr_orig += 40;
    }

    // 5. 构造并发送响应
    std::vector<char> response_payload_co;
    response_payload_co.resize(sizeof(uint32_t) + results_status_bytes.size());
    std::memcpy(response_payload_co.data(), &num_hashes_net, sizeof(uint32_t));
    if (!results_status_bytes.empty()) {
        std::memcpy(response_payload_co.data() + sizeof(uint32_t), results_status_bytes.data(), results_status_bytes.size());
    }
    session->Send(response_payload_co, Protocol::MSG_RESP_CHECK_OBJECTS_RESULT); //
}


/**
 * @brief 处理客户端发送的 MSG_REQ_PUT_OBJECT (上传对象) 请求。
 * 1. 认证客户端：从消息体提取并验证Token。
 * - 协议约定消息体格式为: <token_str_with_null_term>\0<40_char_sha1_hash_from_client><actual_raw_git_object_data>
 * 2. 检查仓库是否选定。
 * 3. 从原始载荷中解析出客户端声称的对象哈希 (40字节) 和对象的原始数据。
 * 4. 校验客户端提供的哈希是否与接收到的对象数据的实际哈希匹配。
 * 5. 如果哈希匹配，则调用活动仓库的 write_raw_object 方法将对象数据写入对象库。
 * 6. 根据写入结果：
 * - 如果成功，发送 MSG_RESP_ACK_OK，消息体可以包含已验证的对象哈希。
 * - 如果失败（哈希不匹配、写入错误等），发送 MSG_RESP_ERROR。
 * @param session 指向 CSession 的共享指针。
 * @param msg_id 消息ID (应为 Protocol::MSG_REQ_PUT_OBJECT)。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 */
void LogicSystem::HandleReqPutObject(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data_with_token, uint32_t body_length_with_token) {
    if (!session || session->IsClosed()) return;

    const char* original_body_ptr = nullptr;
    uint32_t original_body_len = 0;
    std::string username_from_token;

    // 1. 进行认证并准备载荷
    if (!authenticateAndPreparePayload(session, body_data_with_token, body_length_with_token, "PUT_OBJECT", original_body_ptr, original_body_len, username_from_token)) {
        return;
    }

    // 2. 检查仓库是否已选定
    if (!session->IsRepositorySelected()) { session->Send("No repository selected for PUT_OBJECT.", Protocol::MSG_RESP_ERROR); return; }
    std::shared_ptr<Repository> active_repo = session->GetActiveRepository();
    if (!active_repo) { session->Send("Server internal error: repo context lost for PUT_OBJECT.", Protocol::MSG_RESP_ERROR); return; }

    // 3. 解析原始载荷: <40_char_sha1_hash_from_client><actual_raw_git_object_data>
    if (original_body_len <= 40) { session->Send("Invalid payload for PUT_OBJECT (too short for hash+data).", Protocol::MSG_RESP_ERROR); return; }
    std::string object_hash_from_client(original_body_ptr, 40);
    const char* actual_object_raw_data = original_body_ptr + 40;
    uint32_t actual_object_data_len = original_body_len - 40;

    // 3a. 基本的哈希格式校验
    if (object_hash_from_client.length() != 40 || !std::all_of(object_hash_from_client.begin(), object_hash_from_client.end(), ::isxdigit)) {
         session->Send("Invalid object hash format in PUT_OBJECT request.", Protocol::MSG_RESP_ERROR); return;
    }

    // 4. 数据校验：重新计算接收到的对象数据的SHA1哈希
    std::vector<std::byte> object_bytes_for_hash_calc(actual_object_data_len);
    if (actual_object_data_len > 0) { // 允许空对象数据，例如空 blob 序列化后不为0但其内容部分为0
        std::memcpy(object_bytes_for_hash_calc.data(), reinterpret_cast<const std::byte*>(actual_object_raw_data), actual_object_data_len);
    }
    std::string calculated_hash = SHA1::sha1(object_bytes_for_hash_calc); //
    if (calculated_hash != object_hash_from_client) {
        session->Send("Object data hash mismatch.", Protocol::MSG_RESP_ERROR); return;
    }

    // 5. 对象写入
    bool write_ok = active_repo->write_raw_object(calculated_hash, actual_object_raw_data, actual_object_data_len); //

    // 6. 发送响应
    if (write_ok) {
        session->Send(calculated_hash, Protocol::MSG_RESP_ACK_OK); //
    } else {
        session->Send("Server error: failed to write object for PUT_OBJECT.", Protocol::MSG_RESP_ERROR); //
    }
}


/**
 * @brief 处理客户端发送的 MSG_REQ_UPDATE_REF (更新引用) 请求。
 * 1. 认证客户端。
 * - 协议约定消息体格式为: <token_str_with_null_term>\0<force_flag_byte><ref_name_str_with_null_term>...
 * 2. 检查仓库是否选定。
 * 3. 从原始载荷中解析出 force_flag、要更新的引用全名、新的commit哈希以及可选的旧commit哈希。
 * 4. 调用活动仓库的 update_ref 方法尝试更新引用。
 * 5. 根据 update_ref 的结果（成功、失败、拒绝等），向客户端发送相应的响应消息
 * (MSG_RESP_REF_UPDATED, MSG_RESP_REF_UPDATE_DENIED, MSG_RESP_ERROR)。
 * @param session 指向 CSession 的共享指针。
 * @param msg_id 消息ID (应为 Protocol::MSG_REQ_UPDATE_REF)。
 * @param body_data_with_token 指向包含Token前缀的完整消息体的指针。
 * @param body_length_with_token 完整消息体的总长度。
 */
void LogicSystem::HandleReqUpdateRef(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data_with_token, uint32_t body_length_with_token) {
    if (!session || session->IsClosed()) return;

    const char* original_body_ptr = nullptr;
    uint32_t original_body_len = 0;
    std::string username_from_token;

    // 1. 进行认证并准备载荷
    if (!authenticateAndPreparePayload(session, body_data_with_token, body_length_with_token, "UPDATE_REF", original_body_ptr, original_body_len, username_from_token)) {
        return;
    }

    // 2. 检查仓库是否选定
    if (!session->IsRepositorySelected()) { session->Send("No repository selected for UPDATE_REF.", Protocol::MSG_RESP_ERROR); return; }
    std::shared_ptr<Repository> active_repo = session->GetActiveRepository();
    if (!active_repo) { session->Send("Server internal error: repo context lost for UPDATE_REF.", Protocol::MSG_RESP_ERROR); return; }

    // 3. 解析原始载荷: <force_flag_byte><ref_name_str_null_term><new_hash_40char>[<optional_old_hash_40char_if_any>]
    // 最小长度: 1 (force_flag) + 1 (min ref_name) + 1 (\0 for ref_name) + 40 (new_hash) = 43 bytes
    if (original_body_len < (1 + 1 + 1 + 40)) {
        session->Send("Invalid payload for UPDATE_REF (too short for content).", Protocol::MSG_RESP_ERROR); return;
    }

    const char* ptr_orig = original_body_ptr;
    // 3a. 解析 force_flag
    bool client_requests_force_update = (*ptr_orig == static_cast<char>(0x01));
    ptr_orig++;
    uint32_t len_after_force = original_body_len - 1;

    // 3b. 解析 ref_name
    const char* ref_name_start_orig = ptr_orig;
    const char* ref_name_end_orig = static_cast<const char*>(memchr(ref_name_start_orig, '\0', len_after_force));
    if (!ref_name_end_orig || ref_name_end_orig == ref_name_start_orig) { session->Send("Invalid payload (ref_name format error for UPDATE_REF).", Protocol::MSG_RESP_ERROR); return; }
    std::string ref_name_str(ref_name_start_orig, ref_name_end_orig - ref_name_start_orig);
    size_t ref_name_len_with_null_orig = (ref_name_end_orig - ref_name_start_orig) + 1;

    // 检查剩余长度是否足够存放 new_hash
    if (ref_name_len_with_null_orig + 40 > len_after_force) { session->Send("Invalid payload (new_hash missing for UPDATE_REF).", Protocol::MSG_RESP_ERROR); return; }
    ptr_orig = ref_name_end_orig + 1;

    // 3c. 解析 new_hash
    std::string new_hash_str(ptr_orig, 40);
    ptr_orig += 40;
    uint32_t remaining_for_old_hash = len_after_force - (ref_name_len_with_null_orig + 40);

    // 3d. 解析 optional_old_hash
    std::optional<std::string> expected_old_hash_opt = std::nullopt;
    if (remaining_for_old_hash == 40) {
        std::string old_hash_str_temp(ptr_orig, 40);
        if (old_hash_str_temp.length() == 40 && std::all_of(old_hash_str_temp.begin(), old_hash_str_temp.end(), ::isxdigit)) {
            expected_old_hash_opt = old_hash_str_temp;
        } else {
            session->Send("Invalid optional_old_hash format for UPDATE_REF.", Protocol::MSG_RESP_ERROR); return;
        }
    } else if (remaining_for_old_hash != 0) { // 如果有剩余但不是40字节，则格式错误
        session->Send("Invalid payload (old_hash length error for UPDATE_REF).", Protocol::MSG_RESP_ERROR); return;
    }

    // 4. 调用 Repository 的 update_ref 方法
    Repository::UpdateRefResult result = active_repo->update_ref(ref_name_str, new_hash_str, expected_old_hash_opt, client_requests_force_update); //

    // 5. 根据结果发送响应
    std::string response_body_str_ur;
    switch (result) {
        case Repository::UpdateRefResult::SUCCESS:
            response_body_str_ur = ref_name_str; response_body_str_ur.push_back('\0'); response_body_str_ur.append(new_hash_str);
            if(expected_old_hash_opt) {response_body_str_ur.push_back('\0'); response_body_str_ur.append(*expected_old_hash_opt); }
            response_body_str_ur.push_back('\0'); // 确保整个 value 也是以 \0 结尾的字符串
            session->Send(response_body_str_ur, Protocol::MSG_RESP_REF_UPDATED); //
            break;
        case Repository::UpdateRefResult::REF_NOT_FOUND_FOR_UPDATE:
            session->Send("Ref not found for update (expected old hash): " + ref_name_str, Protocol::MSG_RESP_REF_UPDATE_DENIED); //
            break;
        case Repository::UpdateRefResult::OLD_HASH_MISMATCH:
            session->Send("Update rejected: current tip of ref '" + ref_name_str + "' does not match expected old hash.", Protocol::MSG_RESP_REF_UPDATE_DENIED); //
            break;
        case Repository::UpdateRefResult::NEW_COMMIT_NOT_FOUND:
            session->Send("Update rejected: new commit object '" + new_hash_str.substr(0,7) + "' not found on server.", Protocol::MSG_RESP_REF_UPDATE_DENIED); //
            break;
        case Repository::UpdateRefResult::NOT_FAST_FORWARD:
            session->Send("Update rejected: ref '" + ref_name_str + "' update is not fast-forward.", Protocol::MSG_RESP_REF_UPDATE_DENIED); //
            break;
        case Repository::UpdateRefResult::INVALID_REF_NAME:
            session->Send("Invalid ref name format: " + ref_name_str, Protocol::MSG_RESP_ERROR); //
            break;
        case Repository::UpdateRefResult::IO_ERROR: // Fallthrough
        default:
             session->Send("Server error during ref update of '" + ref_name_str + "'.", Protocol::MSG_RESP_ERROR); //
            break;
    }
}


} // namespace Biogit