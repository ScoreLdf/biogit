#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <boost/asio/detail/socket_ops.hpp>

namespace Biogit {

namespace Protocol {

// -------------------- 头部定义 --------------------
// 总头部长度 = 消息ID长度 + 消息体长度字段的长度
// 消息ID: 2字节
// 消息体长度: 4字节
const uint16_t HEAD_ID_LEN = 2; // 消息ID长度
const uint16_t HEAD_DATA_LEN_FIELD = 4; // 消息体长度字段的长度
const uint16_t HEAD_TOTAL_LEN = HEAD_ID_LEN + HEAD_DATA_LEN_FIELD; // 总头部长度 = 消息ID长度 + 消息体长度字段的长度


// -------------------- Biogit 特定消息ID --------------------
// C -> S (Client to Server Requests)
const uint16_t MSG_REQ_UNKNOWN = 0;                  // 未知或错误请求

// --- 仓库请求ID ---
const uint16_t MSG_REQ_LIST_REFS = 2001;             // 客户端请求服务器所有引用 (HEAD、分支和标签)
const uint16_t MSG_REQ_GET_OBJECT = 2002;            // 客户端请求获取特定哈希的 Git 对象
const uint16_t MSG_REQ_CHECK_OBJECTS = 2003;         // 客户端发送一批哈希，询问服务器哪些已存在
const uint16_t MSG_REQ_PUT_OBJECT = 2004;            // 客户端准备发送一个完整的 Git 对象
const uint16_t MSG_REQ_UPDATE_REF = 2005;            // 客户端请求服务器更新某个引用
const uint16_t MSG_REQ_TARGET_REPO = 2010;           // 客户端指定目标仓库路径

// --- 用户认证请求ID ---
const uint16_t MSG_REQ_REGISTER_USER = 2020;        // 客户端请求注册新用户
const uint16_t MSG_REQ_LOGIN_USER = 2021;           // 客户端请求登录


// S -> C (Server to Client Responses)
const uint16_t MSG_RESP_ACK_OK = 3001;               // 通用成功应答
const uint16_t MSG_RESP_ERROR = 3002;                // 通用错误应答

// --- 仓库响应ID ---
const uint16_t MSG_RESP_REFS_LIST_BEGIN = 3003;      // 服务器开始发送引用列表
const uint16_t MSG_RESP_REFS_ENTRY = 3004;           // 服务器发送单条引用信息
const uint16_t MSG_RESP_REFS_LIST_END = 3005;        // 服务器引用列表发送完毕
const uint16_t MSG_RESP_OBJECT_CONTENT = 3006;       // 服务器发送 Git 对象的原始内容
const uint16_t MSG_RESP_OBJECT_NOT_FOUND = 3007;     // 服务器未找到客户端请求的对象
const uint16_t MSG_RESP_CHECK_OBJECTS_RESULT = 3008; // 服务器响应对象存在性检查
const uint16_t MSG_RESP_REF_UPDATED = 3009;          // 服务器成功更新了引用
const uint16_t MSG_RESP_REF_UPDATE_DENIED = 3010;    // 服务器拒绝更新引用
const uint16_t MSG_RESP_TARGET_REPO_ACK = 3020;      // 服务器确认仓库已选定
const uint16_t MSG_RESP_TARGET_REPO_ERROR = 3021;    // 服务器无法找到或加载仓库

// --- 用户认证响应ID ---
const uint16_t MSG_RESP_REGISTER_SUCCESS = 3030;     // 用户注册成功
const uint16_t MSG_RESP_REGISTER_FAILURE = 3031;     // 用户注册失败
const uint16_t MSG_RESP_LOGIN_SUCCESS = 3032;        // 用户登录成功 (响应体中包含Token)
const uint16_t MSG_RESP_LOGIN_FAILURE = 3033;        // 用户登录失败
const uint16_t MSG_RESP_AUTH_REQUIRED = 3034;        // 服务器提示需要认证或Token无效


// --- Test Message IDs ---
const uint16_t MSG_TEST_ECHO_REQ = 1;
const uint16_t MSG_TEST_ECHO_RESP = 2;
const uint16_t MSG_TEST_PING_REQ = 3;
const uint16_t MSG_TEST_PONG_RESP = 4;


// -------------------- 辅助函数 - 用于打包和解包消息头部 --------------------
inline void pack_header(char* buffer, uint16_t msg_id, uint32_t body_length) {
    uint16_t msg_id_net = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
    uint32_t body_length_net = boost::asio::detail::socket_ops::host_to_network_long(body_length);

    std::memcpy(buffer, &msg_id_net, HEAD_ID_LEN);
    std::memcpy(buffer + HEAD_ID_LEN, &body_length_net, HEAD_DATA_LEN_FIELD);
}

inline bool unpack_header(const char* buffer, uint16_t& msg_id, uint32_t& body_length) {
    uint16_t msg_id_net;
    uint32_t body_length_net;

    std::memcpy(&msg_id_net, buffer, HEAD_ID_LEN);
    std::memcpy(&body_length_net, buffer + HEAD_ID_LEN, HEAD_DATA_LEN_FIELD);

    msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id_net);
    body_length = boost::asio::detail::socket_ops::network_to_host_long(body_length_net);
    return true;
}


// -------------------- 消息体内容的约定 --------------------
/*
    ==============================================
    Part 1: 客户端 -> 服务器 (C -> S) 请求消息
    ==============================================

    MSG_REQ_UNKNOWN (0):
        Body: (不适用或未定义)
        说明: 通常不应发送此ID。

    ----------------------------------------------
    A. 无需认证即可访问的请求 (或认证是可选的)
    ----------------------------------------------
    (当前设计中，以下认证相关请求本身无需Token)

    MSG_REQ_REGISTER_USER (2020):
        Body: <username_str_with_null_term><password_str_with_null_term>
        说明: 用户名和密码字符串依次拼接，每个字符串都以 '\0' 结尾。
        示例: "newuser\0securepass123\0"

    MSG_REQ_LOGIN_USER (2021):
        Body: <username_str_with_null_term><password_str_with_null_term>
        说明: 同 MSG_REQ_REGISTER_USER。

    -----------------------------------------------------------------
    B. 需要认证才能访问的请求 (消息体前缀统一为 Token)
       格式: <token_str_with_null_term>\0<actual_request_payload>
    -----------------------------------------------------------------

    MSG_REQ_TARGET_REPO (2010):
        Actual Request Payload: <repo_relative_path_str_with_null_term>
        说明: 指定服务器上要操作的仓库的相对路径 (例如 "userA/projectX\0")。
        完整 Body: <token_str_with_null_term>\0<repo_relative_path_str_with_null_term>

    MSG_REQ_LIST_REFS (2001):
        Actual Request Payload: (空)
        说明: 请求服务器上当前目标仓库的所有引用。
        完整 Body: <token_str_with_null_term>\0
              (Token 后的内容为空，但分隔符 '\0' 存在，表示原始消息体部分长度为0)

    MSG_REQ_GET_OBJECT (2002):
        Actual Request Payload: <40_char_sha1_hash_string>
        说明: 请求获取特定哈希的 Git 对象。哈希字符串固定40字节，不含 '\0'。
        完整 Body: <token_str_with_null_term>\0<40_char_sha1_hash_string>

    MSG_REQ_CHECK_OBJECTS (2003):
        Actual Request Payload: <num_hashes_uint32_t_net><40_char_sha1_1><40_char_sha1_2>...
        说明: 客户端发送一批对象哈希（数量由 num_hashes_uint32_t_net 指定，网络字节序），
              询问服务器这些对象哪些已存在。哈希字符串紧密排列，每个40字节。
        完整 Body: <token_str_with_null_term>\0<num_hashes_uint32_t_net><40_char_sha1_1>...

    MSG_REQ_PUT_OBJECT (2004):
        Actual Request Payload: <40_char_sha1_hash_from_client><actual_raw_git_object_data>
        说明: 客户端准备发送一个完整的 Git 对象。前40字节是客户端声称的对象哈希，
              后面紧跟对象的完整原始内容 (包含 "type size\0content")。
        完整 Body: <token_str_with_null_term>\0<40_char_sha1_hash_from_client><actual_raw_git_object_data>

    MSG_REQ_UPDATE_REF (2005):
        Actual Request Payload: <force_flag_byte><ref_name_str_with_null_term><new_hash_40char>[<optional_old_hash_40char_if_any>]
        说明: 请求服务器更新某个引用。
              - force_flag_byte (1字节): 0x01 表示强制，0x00 表示非强制。
              - ref_name_str_with_null_term: 要更新的完整引用名 (例如 "refs/heads/main\0")。
              - new_hash_40char (40字节): 新的 commit 哈希。
              - optional_old_hash_40char_if_any (可选的40字节): 期望的旧 commit 哈希。如果提供，则其紧跟 new_hash。
        完整 Body: <token_str_with_null_term>\0<force_flag_byte><ref_name_str_with_null_term><new_hash_40char>[<optional_old_hash_40char_if_any>]


    ----------------------------------------------
    C. 测试消息 (通常无需认证)
    ----------------------------------------------
    MSG_TEST_ECHO_REQ (1):
        Body: <any_string_with_null_term>
        说明: 客户端发送任意以 '\0' 结尾的字符串。

    MSG_TEST_PING_REQ (3):
        Body: (空)

    =================================================
    Part 2: 服务器 -> 客户端 (S -> C) 响应消息
    =================================================

    MSG_RESP_ACK_OK (3001):
        Body: (可选) <message_str_with_null_term>
        说明: 通用成功应答。消息体可以为空，或包含一个成功的描述信息。
              例如，对 PUT_OBJECT 成功接收后，Body 可以是该对象的40字节哈希 (不含 '\0')。

    MSG_RESP_ERROR (3002):
        Body: (可选) <error_code_uint16_t_net><error_message_str_with_null_term>
        说明: 通用错误应答。消息体可以为空，或包含一个网络字节序的错误码，后跟一个描述错误的字符串。

    MSG_RESP_REFS_LIST_BEGIN (3003):
        Body: (空)
        说明: 标志服务器开始发送引用列表。

    MSG_RESP_REFS_ENTRY (3004):
        Body: <ref_name_str_with_null_term><ref_value_str_with_null_term>
        说明: 服务器发送的单条引用信息。引用名和引用值都以 '\0' 结尾并依次拼接。
              - 对于 "HEAD" 引用，<ref_value_str> 可能是符号引用如 "ref: refs/heads/main"。
              - 对于其他引用 (如 "refs/heads/main", "refs/tags/v1.0")，<ref_value_str> 通常是40字节的commit哈希字符串。
        示例1: "HEAD\0ref: refs/heads/main\0"
        示例2: "refs/heads/main\0<40-char-hash>\0"

    MSG_RESP_REFS_LIST_END (3005):
        Body: (空)
        说明: 标志服务器引用列表发送完毕。

    MSG_RESP_OBJECT_CONTENT (3006):
        Body: <40_char_sha1_hash_string_of_object><actual_raw_git_object_data>
        说明: 服务器发送 Git 对象的原始内容。前40字节是被请求对象的哈希，
              后面是对象的完整原始内容 (包含 "type size\0content")。

    MSG_RESP_OBJECT_NOT_FOUND (3007):
        Body: <40_char_requested_hash_string>
        说明: 服务器未找到客户端请求的对象。消息体是被请求的40字节哈希。

    MSG_RESP_CHECK_OBJECTS_RESULT (3008):
        Body: <num_results_uint32_t_net><status_byte_1><status_byte_2>...
        说明: 服务器响应对象存在性检查。
              - num_results_uint32_t_net: 结果数量 (网络字节序)，应与请求中的哈希数量相同。
              - status_byte_n (1字节): 每个字节表示对应请求哈希的状态，0x01 表示存在, 0x00 表示不存在。

    MSG_RESP_REF_UPDATED (3009):
        Body: (可选) <ref_name_str_with_null_term><new_hash_40char_after_update>
        说明: 服务器成功更新了引用。消息体可以为空，或包含被更新的引用名和它现在指向的新哈希。

    MSG_RESP_REF_UPDATE_DENIED (3010):
        Body: (可选) <reason_message_str_with_null_term>
        说明: 服务器拒绝更新引用。消息体可以为空，或包含拒绝的原因。

    MSG_RESP_TARGET_REPO_ACK (3020):
        Body: (可选) <success_message_str_with_null_term>
        说明: 服务器确认仓库已成功选定。消息体可以为空或包含确认信息。

    MSG_RESP_TARGET_REPO_ERROR (3021):
        Body: (可选) <error_message_str_with_null_term>
        说明: 服务器在选定目标仓库时发生错误（例如，找不到或无法加载）。

    ----------------------------------------------
    D. 认证相关响应
    ----------------------------------------------
    MSG_RESP_REGISTER_SUCCESS (3030):
        Body: (可选) <success_message_str_with_null_term>
        说明: 用户注册成功。

    MSG_RESP_REGISTER_FAILURE (3031):
        Body: (可选) <error_message_str_with_null_term>
        说明: 用户注册失败 (例如，用户名已存在，密码太弱等)。

    MSG_RESP_LOGIN_SUCCESS (3032):
        Body: <token_str_with_null_term>
        说明: 用户登录成功。消息体是服务器生成的认证 Token 字符串。

    MSG_RESP_LOGIN_FAILURE (3033):
        Body: (可选) <error_message_str_with_null_term>
        说明: 用户登录失败 (例如，用户名或密码错误)。

    MSG_RESP_AUTH_REQUIRED (3034):
        Body: (可选) <message_str_with_null_term>
        说明: 服务器提示当前操作需要认证，或者客户端提供的 Token 无效/已过期。


    ----------------------------------------------
    E. 测试响应
    ----------------------------------------------
    MSG_TEST_ECHO_RESP (2):
        Body: <same_string_as_req_with_null_term>
        说明: 服务器原样返回客户端在 MSG_TEST_ECHO_REQ 中发送的字符串。

    MSG_TEST_PONG_RESP (4):
        Body: (空)
*/

} // namespace Protocol
} // namespace Biogit