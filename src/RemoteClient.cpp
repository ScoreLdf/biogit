#include <iostream>
#include "../include/RemoteClient.h"
namespace Biogit {

/**
 * @brief RemoteClient 构造函数。
 * @param io_context Boost.Asio的io_context，用于所有同步网络操作。
 */
RemoteClient::RemoteClient(boost::asio::io_context& io_context)
    : _io_context(io_context),
      _socket(io_context),    // 初始化 socket
      _resolver(io_context),  // 初始化 resolver
      _is_connected(false) {
    // std::cout << "RemoteClient: Instance created." << std::endl;
}

/**
 * @brief RemoteClient 析构函数。
 * 如果客户端仍然连接，则尝试断开连接。
 */
RemoteClient::~RemoteClient() {
    // std::cout << "RemoteClient: Destructing..." << std::endl;
    if (_is_connected) {
        Disconnect();
    }
    // std::cout << "RemoteClient: Destructed." << std::endl;
}

/**
 * @brief 连接到指定的 biogit2 服务器。
 * @param host 服务器的主机名或IP地址。
 * @param port_str 服务器的端口号字符串。
 * @return 如果连接成功则为 true，否则为 false。
 */
bool RemoteClient::Connect(const std::string& host, const std::string& port_str) {
    if (_is_connected) {
        Disconnect(); // 如果已连接，先断开
    }
    try {
        // std::cout << "RemoteClient: Connecting to " << host << ":" << port_str << "..." << std::endl;
        boost::asio::ip::tcp::resolver::results_type endpoints = _resolver.resolve(host, port_str);
        boost::asio::connect(_socket, endpoints); // 同步连接
        _is_connected = true;
        // std::cout << "RemoteClient: Connected successfully to " << host << ":" << port_str << "." << std::endl;
        return true;
    } catch (const boost::system::system_error& e) {
        std::cerr << "RemoteClient: Connection failed to " << host << ":" << port_str << " - " << e.what() << std::endl;
        _is_connected = false;
        if (_socket.is_open()) { // 确保socket在连接失败时是关闭状态
            boost::system::error_code ec_close;
            _socket.close(ec_close);
        }
        return false;
    }
}

/**
 * @brief 断开与服务器的连接。
 * 会尝试优雅地关闭socket。
 */
void RemoteClient::Disconnect() {
    if (!_is_connected && !_socket.is_open()) { // 如果未连接且socket未打开，则无需操作
        return;
    }
    // std::cout << "RemoteClient: Disconnecting..." << std::endl;
    boost::system::error_code ec;
    // 尝试优雅关闭：先 shutdown 双向数据流
    _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    // if (ec && ec != boost::asio::error::not_connected) { // "not_connected" 错误在shutdown时是正常的，如果socket已经关闭
    //     std::cerr << "RemoteClient: Error during socket shutdown: " << ec.message() << std::endl;
    // }
    _socket.close(ec); // 然后关闭socket
    // if (ec) {
    //     std::cerr << "RemoteClient: Error during socket close: " << ec.message() << std::endl;
    // }
    _is_connected = false;
    // std::cout << "RemoteClient: Disconnected." << std::endl;
}

/**
 * @brief 检查客户端当前是否已连接到服务器并且socket是打开的。
 * @return 如果已连接且socket打开则为 true，否则为 false。
 */
bool RemoteClient::IsConnected() const {
    return _is_connected && _socket.is_open();
}


/**
 * @brief （私有辅助函数）发送一个完整的请求消息并同步接收一个完整的响应消息。
 * @param request_node 包含待发送请求数据的 SendNode 对象。
 * @param out_response_id (输出参数) 成功接收响应时，填充响应消息的ID。
 * @param out_response_body (输出参数) 成功接收响应时，填充响应消息的消息体。
 * @param operation_context_for_logging 用于日志的操作上下文描述。
 * @return 如果请求发送和完整响应接收都成功，则返回 true；否则返回 false。
 */
bool RemoteClient::SendAndReceive(const SendNode& request_node,
                                  uint16_t& out_response_id,
                                  std::vector<char>& out_response_body,
                                  const std::string& operation_context_for_logging) {
    if (!IsConnected()) {
        std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Not connected." << std::endl;
        return false;
    }

    boost::system::error_code error;
    // 同步发送请求数据
    boost::asio::write(_socket, boost::asio::buffer(request_node.data(), request_node.total_length()), error);
    if (error) {
        std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Failed to send request: " << error.message() << std::endl;
        Disconnect();
        return false;
    }

    // 同步接收响应消息
    return ReceiveFullMessage(out_response_id, out_response_body, operation_context_for_logging + "_RESP");
}

/**
 * @brief （私有辅助函数）同步接收一个完整的消息（头部 + 消息体）。
 * @param out_id (输出参数) 成功解析头部后，填充消息的ID。
 * @param out_body (输出参数) 成功接收后，填充消息的消息体。
 * @param operation_context_for_logging 用于日志的操作上下文描述。
 * @return 如果成功接收并解析了完整的消息，则返回 true；否则返回 false。
 */
bool RemoteClient::ReceiveFullMessage(uint16_t& out_id,
                                      std::vector<char>& out_body,
                                      const std::string& operation_context_for_logging) {
    if (!IsConnected()) {
        std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Not connected for receive." << std::endl;
        return false;
    }
    try {
        char header_buf[Protocol::HEAD_TOTAL_LEN];
        boost::system::error_code error;

        // 1. 同步读取消息头部
        size_t len_header = boost::asio::read(_socket, boost::asio::buffer(header_buf, Protocol::HEAD_TOTAL_LEN), error);
        if (error == boost::asio::error::eof) {
            std::cerr << "RemoteClient Info [" << operation_context_for_logging << "]: Connection closed by server while reading header (EOF)." << std::endl;
            Disconnect(); return false;
        } else if (error) {
            std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Reading header: " << error.message() << std::endl;
            Disconnect(); return false;
        }
        if (len_header != Protocol::HEAD_TOTAL_LEN) {
            std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Incomplete header (got " << len_header << " bytes)." << std::endl;
            Disconnect(); return false;
        }

        // 2. 解析消息头部
        uint32_t body_len;
        Protocol::unpack_header(header_buf, out_id, body_len); //

        // 3. 根据解析出的长度，读取消息体
        out_body.resize(body_len);
        if (body_len > 0) {
            size_t len_body = boost::asio::read(_socket, boost::asio::buffer(out_body), error);
            if (error == boost::asio::error::eof) {
                std::cerr << "RemoteClient Info [" << operation_context_for_logging << "]: Connection closed by server while reading body (EOF)." << std::endl;
                Disconnect(); return false;
            } else if (error) {
                std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Reading body: " << error.message() << std::endl;
                Disconnect(); return false;
            }
            if (len_body != body_len) {
                std::cerr << "RemoteClient Error [" << operation_context_for_logging << "]: Incomplete body (expected " << body_len << ", got " << len_body << " bytes)." << std::endl;
                Disconnect(); return false;
            }
        }
        return true;
    } catch (const boost::system::system_error& bse) {
         std::cerr << "RemoteClient Exception [" << operation_context_for_logging << "] in ReceiveFullMessage: " << bse.what() << std::endl;
         Disconnect();
         return false;
    }
}

/**
 * @brief （私有辅助函数）构造带有Token前缀的完整消息体。
 * 协议: <token_str_with_null_term>\0<original_defined_message_body>
 * @param token 认证 Token 字符串 (不应包含其自身的末尾 '\0'，此函数会添加 Token 后的第一个 '\0')。
 * @param original_payload 指向原始业务数据载荷的指针。
 * @param original_payload_len 原始业务数据载荷的长度。
 * @return 一个 std::vector<char>，包含了组合后的完整消息体数据。
 */
std::vector<char> RemoteClient::buildPayloadWithToken(const std::string& token, const char* original_payload, uint32_t original_payload_len) {
    std::vector<char> final_payload;
    // 容量: Token内容长度 + Token的'\0'分隔符 + 原始载荷长度
    final_payload.reserve(token.length() + 1 + original_payload_len);

    // 1. 添加 Token 字符串内容
    final_payload.insert(final_payload.end(), token.begin(), token.end());
    // 2. 添加 Token 字符串的末尾空字符 '\0'，它同时也作为 Token 和原始载荷之间的分隔符。
    final_payload.push_back('\0');
    // 3. 添加原始业务数据载荷 (如果存在)
    if (original_payload != nullptr && original_payload_len > 0) {
        final_payload.insert(final_payload.end(), original_payload, original_payload + original_payload_len);
    }
    // 如果原始载荷为空 (original_payload_len == 0)，则 final_payload 仅包含 token + '\0'。
    // 服务器端的 LogicSystem::extractTokenAndPayload 会正确处理这种情况，
    // 将 original_payload_len 解析为0。
    return final_payload;
}

/**
 * @brief （私有辅助函数）buildPayloadWithToken 的重载版本，接受 std::string 作为原始载荷。
 * @note 原始载荷字符串 (original_payload_str) 的 .data() 和 .length() 将被用于构造。
 * 如果协议要求原始载荷本身也以 '\0' 结尾，调用者需确保 original_payload_str 已包含。
 */
std::vector<char> RemoteClient::buildPayloadWithToken(const std::string& token, const std::string& original_payload_str) {
    return buildPayloadWithToken(token, original_payload_str.data(), static_cast<uint32_t>(original_payload_str.length()));
}


/**
 * @brief 向服务器发送 MSG_REQ_REGISTER_USER 请求以注册新用户。
 * @param username 要注册的用户名。
 * @param password 用户的密码。
 * @param out_server_message (输出参数) 服务器返回的消息 (成功或失败原因)。
 * @return 如果服务器响应注册成功 (MSG_RESP_REGISTER_SUCCESS)，则为 true；否则为 false。
 */
bool RemoteClient::RegisterUser(const std::string& username, const std::string& password, std::string& out_server_message) {
    out_server_message.clear();
    if (username.empty() || password.empty()) {
        out_server_message = "Username and password cannot be empty.";
        return false;
    }

    // 构造消息体: <username_null_terminated>\0<password_null_terminated>
    // 根据 protocol.h 的约定，两个字符串都以 \0 结尾然后拼接。
    std::string payload_str = username;
    payload_str.push_back('\0');
    payload_str.append(password);
    payload_str.push_back('\0');

    SendNode request(payload_str.data(), static_cast<uint32_t>(payload_str.length()), Protocol::MSG_REQ_REGISTER_USER); //
    uint16_t response_id;
    std::vector<char> response_body;

    if (SendAndReceive(request, response_id, response_body, "REGISTER_USER")) {
        if (!response_body.empty()) {
            // 服务器返回的消息体 (例如错误信息) 也可能是以 '\0' 结尾的字符串
            if (response_body.back() == '\0') response_body.pop_back();
            out_server_message.assign(response_body.begin(), response_body.end());
        } else if (response_id == Protocol::MSG_RESP_REGISTER_SUCCESS) { //
            out_server_message = "Registration successful (no message from server).";
        } else { // 其他失败情况且响应体为空
            out_server_message = "Registration failed (no specific message from server).";
        }
        return response_id == Protocol::MSG_RESP_REGISTER_SUCCESS;
    }
    out_server_message = "Network error during registration.";
    return false;
}

/**
 * @brief 向服务器发送 MSG_REQ_LOGIN_USER 请求以登录。
 * @param username 用户名。
 * @param password 密码。
 * @param out_token (输出参数) 如果登录成功，这里将包含服务器返回的认证 Token (不含末尾'\0')。
 * @param out_server_message (输出参数) 服务器返回的消息 (成功或失败原因)。
 * @return 如果服务器响应登录成功 (MSG_RESP_LOGIN_SUCCESS)，则为 true；否则为 false。
 */
bool RemoteClient::LoginUser(const std::string& username, const std::string& password, std::string& out_token, std::string& out_server_message) {
    out_token.clear();
    out_server_message.clear();
    if (username.empty() || password.empty()) {
        out_server_message = "Username and password cannot be empty.";
        return false;
    }

    // 构造消息体: <username_null_terminated>\0<password_null_terminated>
    std::string payload_str = username;
    payload_str.push_back('\0');
    payload_str.append(password);
    payload_str.push_back('\0');

    SendNode request(payload_str.data(), static_cast<uint32_t>(payload_str.length()), Protocol::MSG_REQ_LOGIN_USER); //
    uint16_t response_id;
    std::vector<char> response_body;

    if (SendAndReceive(request, response_id, response_body, "LOGIN_USER")) {
        // 先尝试提取服务器消息（如果有的话）
        if (!response_body.empty()) {
            // 移除可能的末尾 '\0'
            std::string temp_server_msg_body(response_body.begin(), response_body.end());
            if (temp_server_msg_body.back() == '\0') temp_server_msg_body.pop_back();

            if (response_id == Protocol::MSG_RESP_LOGIN_SUCCESS) { //
                out_token = temp_server_msg_body; // 此时 response_body 就是 <token_str_with_null_term>
                out_server_message = "Login successful."; // 可以考虑是否需要服务器也返回文本消息
            } else {
                out_server_message = temp_server_msg_body; // 登录失败，response_body 是错误信息
            }
        } else { // 服务器响应体为空
             if (response_id == Protocol::MSG_RESP_LOGIN_SUCCESS){
                 out_server_message = "Login reported success but no token received from server (protocol error).";
                 return false; // 即使成功ID，没token也是错
            } else {
                 out_server_message = "Login failed (no specific reason from server).";
            }
        }
        return response_id == Protocol::MSG_RESP_LOGIN_SUCCESS;
    }
    out_server_message = "Network error during login.";
    return false;
}

// --- 修改现有核心网络操作接口的实现 ---

/**
 * @brief 向服务器发送 MSG_REQ_TARGET_REPO 消息，以选定要操作的仓库。
 * 此方法现在需要一个认证 Token。Token 会被添加到请求消息体的前缀。
 * @param token 认证 Token 字符串。
 * @param repo_relative_path 要在服务器上操作的仓库的相对路径。
 * 调用者应确保此字符串按协议约定格式化 (例如，如果需要，包含末尾 '\0')。
 * @return 如果服务器确认仓库选定成功，返回 true；否则返回 false。
 */
bool RemoteClient::TargetRepository(const std::string& token, const std::string& repo_relative_path) {
    // // 原始载荷: <repo_relative_path_str_with_null_term>
    // // 确保 repo_relative_path 本身已经是一个以 '\0' 结尾的C风格字符串的内容
    // std::string original_payload = repo_relative_path;
    // if (original_payload.empty() || original_payload.back() != '\0') {
    //     original_payload.push_back('\0'); // 确保符合协议中对<..._with_null_term>的定义
    // }
    //
    // std::vector<char> payload_with_token = buildPayloadWithToken(token, original_payload.data(), static_cast<uint32_t>(original_payload.length()));
    // SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_TARGET_REPO); //

    std::string payload_to_send = repo_relative_path;
    if (payload_to_send.empty() || payload_to_send.back() != '\0') {
        payload_to_send.push_back('\0');
    }
    SendNode request(payload_to_send.data(), static_cast<uint32_t>(payload_to_send.length()), Protocol::MSG_REQ_TARGET_REPO);

    uint16_t response_id;
    std::vector<char> response_body;
    if (SendAndReceive(request, response_id, response_body, "TARGET_REPO")) {
        if (response_id == Protocol::MSG_RESP_TARGET_REPO_ACK) return true; //
        if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) { //
             std::cerr << "RemoteClient: Authentication required for TargetRepository. Token: '" << token.substr(0,10) << "...'" << std::endl;
        } else if (response_id == Protocol::MSG_RESP_TARGET_REPO_ERROR) { //
            std::string error_msg_from_server = "Server failed to target repository.";
            if (!response_body.empty()) {
                if(response_body.back()=='\0') response_body.pop_back();
                if(!response_body.empty()) error_msg_from_server += " Reason: " + std::string(response_body.begin(), response_body.end());
            }
             std::cerr << "RemoteClient: " << error_msg_from_server << std::endl;
        }
    }
    return false;
}

/**
 * @brief 向服务器发送 MSG_REQ_LIST_REFS 消息，获取远程仓库的所有引用。
 * 此方法现在需要一个认证 Token。
 * @param token 认证 Token 字符串。
 * @return 如果成功获取并解析了引用列表，返回一个 map；否则返回 std::nullopt。
 */
std::optional<std::map<std::string, std::string>> RemoteClient::ListRemoteRefs(const std::string& token) {
    std::vector<char> payload_with_token = buildPayloadWithToken(token, nullptr, 0); // 原始载荷为空
    SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_LIST_REFS); //

    uint16_t response_id;
    std::vector<char> response_body;

    if (!SendAndReceive(request, response_id, response_body, "LIST_REFS_INIT")) {
        return std::nullopt;
    }
    if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) {
         std::cerr << "RemoteClient: Authentication required for ListRemoteRefs." << std::endl;
         return std::nullopt;
    }
    if (response_id != Protocol::MSG_RESP_REFS_LIST_BEGIN) {
        std::cerr << "RemoteClient: Expected REFS_LIST_BEGIN, got ID " << response_id << std::endl;
        return std::nullopt;
    }

    std::map<std::string, std::string> remote_refs_map;
    while (true) {
        if (!ReceiveFullMessage(response_id, response_body, "LIST_REFS_ITEM")) {
            std::cerr << "RemoteClient: Error receiving subsequent messages in ListRemoteRefs." << std::endl;
            return std::nullopt;
        }

        if (response_id == Protocol::MSG_RESP_REFS_ENTRY) {
            std::string entry_str(response_body.data(), response_body.size());
            size_t first_null_pos = entry_str.find('\0');
            // 协议约定: <ref_name_str_with_null_term><ref_value_str_with_null_term>
            if (first_null_pos != std::string::npos && first_null_pos < entry_str.length() -1 ) { // 确保第一个\0后至少有一个字符(第二个\0)
                std::string ref_name = entry_str.substr(0, first_null_pos);
                std::string ref_value_with_tail_null = entry_str.substr(first_null_pos + 1);
                std::string ref_value = ref_value_with_tail_null;
                if(!ref_value.empty() && ref_value.back() == '\0') {
                    ref_value.pop_back(); // 移除值末尾的\0，存储纯净值
                }

                if (ref_name.empty()){ /* Log error or skip */ continue; }
                // HEAD 的值可以是符号引用或哈希，其他引用值应为40位哈希
                if (ref_name == "HEAD" || (ref_value.length() == 40 && std::all_of(ref_value.begin(), ref_value.end(), ::isxdigit)) || !ref_value.empty()) {
                    remote_refs_map[ref_name] = ref_value;
                } else {
                    std::cerr << "RemoteClient: Malformed REFS_ENTRY value for '" << ref_name << "'. Value: '" << ref_value << "'" << std::endl;
                }
            } else {
                 std::cerr << "RemoteClient: Malformed REFS_ENTRY (format error). Raw data size: " << entry_str.length() << std::endl;
            }
        } else if (response_id == Protocol::MSG_RESP_REFS_LIST_END) {
            return remote_refs_map;
        } else if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) {
             std::cerr << "RemoteClient: Authentication became invalid during ListRemoteRefs item processing." << std::endl;
             return std::nullopt;
        } else if (response_id == Protocol::MSG_RESP_ERROR) {
            std::string err_msg = "Server error during ListRemoteRefs items.";
            if(!response_body.empty() && response_body.back() == '\0') response_body.pop_back();
            if(!response_body.empty()) err_msg += " Msg: " + std::string(response_body.begin(), response_body.end());
            std::cerr << "RemoteClient: " << err_msg << std::endl;
            return std::nullopt;
        } else {
            std::cerr << "RemoteClient: Unexpected message ID " << response_id << " while waiting for REFS_ENTRY or REFS_LIST_END." << std::endl;
            return std::nullopt;
        }
    }
}

/**
 * @brief 向服务器发送 MSG_REQ_GET_OBJECT 消息。
 * 此方法现在需要认证 Token。
 * @param token 认证 Token。
 * @param object_hash 要获取的对象的40字节哈希 (不含 '\0')。
 * @param out_received_object_hash_from_server (输出) 服务器响应中包含的哈希。
 * @param out_object_raw_content (输出) 获取到的对象原始内容。
 * @return 服务器响应的消息ID。
 */
uint16_t RemoteClient::GetObject(const std::string& token, const std::string& object_hash,
                                 std::string& out_received_object_hash_from_server,
                                 std::vector<char>& out_object_raw_content) {
    out_received_object_hash_from_server.clear();
    out_object_raw_content.clear();
    if (object_hash.length() != 40) {
        std::cerr << "RemoteClient Error (GetObject): Invalid object_hash parameter length." << std::endl;
        return Protocol::MSG_RESP_ERROR;
    }

    std::vector<char> payload_with_token = buildPayloadWithToken(token, object_hash.data(), static_cast<uint32_t>(object_hash.length()));
    SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_GET_OBJECT); //

    uint16_t response_id;
    std::vector<char> response_body;
    if (SendAndReceive(request, response_id, response_body, "GET_OBJECT")) {
        if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) {
             std::cerr << "RemoteClient: Authentication required for GetObject." << std::endl;
            return response_id;
        }
        if (response_id == Protocol::MSG_RESP_OBJECT_CONTENT) {
            if (response_body.size() >= 40) {
                out_received_object_hash_from_server.assign(response_body.data(), 40);
                out_object_raw_content.assign(response_body.begin() + 40, response_body.end());
                return Protocol::MSG_RESP_OBJECT_CONTENT;
            } else {
                std::cerr << "RemoteClient Error (GetObject): OBJECT_CONTENT response too short." << std::endl;
                return Protocol::MSG_RESP_ERROR;
            }
        } else if (response_id == Protocol::MSG_RESP_OBJECT_NOT_FOUND) {
            if (response_body.size() == 40) { // 响应体是被请求的哈希
                out_received_object_hash_from_server.assign(response_body.data(), 40);
            }
            return Protocol::MSG_RESP_OBJECT_NOT_FOUND;
        }
        return response_id; // 其他服务器响应 (例如 MSG_RESP_ERROR)
    }
    return Protocol::MSG_RESP_ERROR; // SendAndReceive 失败
}

/**
 * @brief  向服务器发送 MSG_REQ_CHECK_OBJECTS 消息。
 * 此方法现在需要认证 Token。
 * @param token 认证 Token。
 * @param object_hashes_to_check 要检查的哈希列表。只处理有效的40位哈希。
 * @param out_existence_results (输出) 检查结果。
 * @return 如果操作成功且收到有效结果，返回 true；否则返回 false。
 */
bool RemoteClient::CheckObjects(const std::string& token, const std::vector<std::string>& object_hashes_to_check,
                                std::vector<ObjectExistenceStatus>& out_existence_results) {
    out_existence_results.clear();
    if (object_hashes_to_check.empty()) return true;

    std::vector<char> original_payload_co;
    std::vector<std::string> valid_hashes_sent_co; // 重命名，与函数参数区分
    original_payload_co.resize(sizeof(uint32_t));
    for(const auto& h_str : object_hashes_to_check) {
        if (h_str.length() == 40 && std::all_of(h_str.begin(), h_str.end(), ::isxdigit)) {
            original_payload_co.insert(original_payload_co.end(), h_str.begin(), h_str.end());
            valid_hashes_sent_co.push_back(h_str);
        }
    }
    if (valid_hashes_sent_co.empty()) return false; // 没有有效哈希可发送
    uint32_t num_h_net = boost::asio::detail::socket_ops::host_to_network_long(static_cast<uint32_t>(valid_hashes_sent_co.size()));
    std::memcpy(original_payload_co.data(), &num_h_net, sizeof(uint32_t));

    std::vector<char> payload_with_token = buildPayloadWithToken(token, original_payload_co.data(), static_cast<uint32_t>(original_payload_co.size()));
    SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_CHECK_OBJECTS); //

    uint16_t response_id;
    std::vector<char> response_body;
    if (SendAndReceive(request, response_id, response_body, "CHECK_OBJECTS")) {
        if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) { std::cerr << "RemoteClient: Auth required for CheckObjects." << std::endl; return false;}
        if (response_id == Protocol::MSG_RESP_CHECK_OBJECTS_RESULT) { //
            if (response_body.size() < sizeof(uint32_t)) return false; // 响应太短
            uint32_t num_results_net_resp;
            std::memcpy(&num_results_net_resp, response_body.data(), sizeof(uint32_t));
            uint32_t num_results_from_server = boost::asio::detail::socket_ops::network_to_host_long(num_results_net_resp);
            if (num_results_from_server != valid_hashes_sent_co.size() || response_body.size() != sizeof(uint32_t) + num_results_from_server) return false; // 长度不匹配

            out_existence_results.reserve(num_results_from_server);
            for (uint32_t i = 0; i < num_results_from_server; ++i) {
                out_existence_results.push_back({valid_hashes_sent_co[i], (response_body[sizeof(uint32_t) + i] == 0x01)});
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 向服务器发送 MSG_REQ_PUT_OBJECT 消息。
 * 此方法现在需要认证 Token。
 * @param token 认证 Token。
 * @param object_hash 对象的40位哈希。
 * @param raw_data 指向对象完整原始内容的指针。
 * @param data_length 对象原始内容的长度。
 * @return 如果服务器确认成功接收 (MSG_RESP_ACK_OK)，返回 true；否则返回 false。
 */
bool RemoteClient::PutObject(const std::string& token, const std::string& object_hash, const char* raw_data, uint32_t data_length) {
    if (object_hash.length() != 40) { std::cerr << "RemoteClient Error (PutObject): Invalid hash length." << std::endl; return false; }
    if (data_length > 0 && raw_data == nullptr) { std::cerr << "RemoteClient Error (PutObject): Data length > 0 but raw_data is null." << std::endl; return false; }

    std::vector<char> original_payload_po;
    original_payload_po.reserve(40 + data_length);
    original_payload_po.insert(original_payload_po.end(), object_hash.begin(), object_hash.end());
    if (data_length > 0) {
        original_payload_po.insert(original_payload_po.end(), raw_data, raw_data + data_length);
    }

    std::vector<char> payload_with_token = buildPayloadWithToken(token, original_payload_po.data(), static_cast<uint32_t>(original_payload_po.size()));
    SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_PUT_OBJECT); //

    uint16_t response_id;
    std::vector<char> response_body;
    if (SendAndReceive(request, response_id, response_body, "PUT_OBJECT")) {
        if (response_id == Protocol::MSG_RESP_AUTH_REQUIRED) { std::cerr << "RemoteClient: Auth required for PutObject." << std::endl; return false; }
        if (response_id == Protocol::MSG_RESP_ACK_OK) { //
            // 响应体应包含已验证的40字节对象哈希
            if (response_body.size() == 40 && std::string(response_body.data(), 40) == object_hash) {
                return true;
            } else if (response_body.empty()){ // 服务器可能不返回响应体
                 std::cout << "RemoteClient Info (PutObject): ACK_OK received with empty response body for " << object_hash.substr(0,7) << std::endl;
                 return true;
            }
            std::cerr << "RemoteClient Warning (PutObject): ACK_OK response body mismatch. Expected " << object_hash.substr(0,7) << std::endl;
            return true; // 收到ACK_OK，即使响应体不符，也认为操作可能已在服务器端成功
        }
    }
    return false;
}

// PutObject 的 vector<char> 重载版本
bool RemoteClient::PutObject(const std::string& token, const std::string& object_hash, const std::vector<char>& raw_object_data_vec){
    return PutObject(token, object_hash, raw_object_data_vec.data(), static_cast<uint32_t>(raw_object_data_vec.size()));
}

/**
 * @brief 向服务器发送 MSG_REQ_UPDATE_REF 消息。
 * 此方法现在需要认证 Token。
 * @param token 认证 Token。
 * @param ref_full_name 要更新的引用全名 (例如 "refs/heads/main")。应由调用者确保其以 '\0' 结尾。
 * @param new_commit_hash 新的 commit 哈希 (40字节，不含 '\0')。
 * @param expected_old_commit_hash (可选) 期望的旧 commit 哈希 (40字节，不含 '\0')。
 * @param force_update true 表示强制更新。
 * @param out_response_body (输出) 服务器的响应消息体。
 * @return 服务器的响应ID。
 */
uint16_t RemoteClient::UpdateRef(const std::string& token, const std::string& ref_full_name,
                                 const std::string& new_commit_hash,
                                 const std::optional<std::string>& expected_old_commit_hash,
                                 bool force_update,
                                 std::vector<char>& out_response_body) {
    out_response_body.clear();
    if (ref_full_name.empty() || new_commit_hash.length() != 40 ||
        (expected_old_commit_hash.has_value() && expected_old_commit_hash->length() != 40)) {
        std::cerr << "RemoteClient Error (UpdateRef): Invalid parameter format/length." << std::endl;
        std::string err_msg = "Invalid parameters for UpdateRef.";
        out_response_body.assign(err_msg.begin(), err_msg.end());
        return Protocol::MSG_RESP_ERROR;
    }

    // 构造原始载荷: <force_flag_byte><ref_name_str_null_term><new_hash_40char>[<old_hash_40char_if_any>]
    std::vector<char> original_payload_ur_vec;
    original_payload_ur_vec.push_back(force_update ? static_cast<char>(0x01) : static_cast<char>(0x00));
    original_payload_ur_vec.insert(original_payload_ur_vec.end(), ref_full_name.begin(), ref_full_name.end());
    original_payload_ur_vec.push_back('\0'); // 确保 ref_full_name 以 null 结尾
    original_payload_ur_vec.insert(original_payload_ur_vec.end(), new_commit_hash.begin(), new_commit_hash.end());
    if (expected_old_commit_hash.has_value()) {
        original_payload_ur_vec.insert(original_payload_ur_vec.end(), expected_old_commit_hash->begin(), expected_old_commit_hash->end());
    }

    std::vector<char> payload_with_token = buildPayloadWithToken(token, original_payload_ur_vec.data(), static_cast<uint32_t>(original_payload_ur_vec.size()));
    SendNode request(payload_with_token.data(), static_cast<uint32_t>(payload_with_token.size()), Protocol::MSG_REQ_UPDATE_REF); //

    uint16_t response_id = Protocol::MSG_RESP_ERROR;
    if (SendAndReceive(request, response_id, out_response_body, "UPDATE_REF")) {
        return response_id; // 服务器的响应ID (可能是 AUTH_REQUIRED, UPDATED, DENIED, ERROR)
    }

    // SendAndReceive 失败
    if (out_response_body.empty()){ // 确保有错误信息
      std::string net_err_msg = "Network error during UpdateRef operation.";
      out_response_body.assign(net_err_msg.begin(), net_err_msg.end());
    }
    return Protocol::MSG_RESP_ERROR;
}

}