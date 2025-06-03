#pragma once

#include <map>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <boost/asio.hpp>
#include "protocol.h" // 包含我们定义的协议
#include "msg_node.h"       // 用于构造发送消息

// 前向声明
namespace Biogit {
// class Repository; // 前向声明

// 封装从服务器收到的引用信息
struct RemoteRefInfo {
    std::string ref_name;    // 例如 "refs/heads/main"
    std::string commit_hash; // 指向的 commit 哈希
};

// 封装从服务器收到的对象存在性检查结果
struct ObjectExistenceStatus {
    std::string requested_hash; // 客户端请求检查的哈希
    bool exists_on_server;   // 服务器上是否存在此对象
};

class RemoteClient {
public:
    /**
     * @brief RemoteClient 构造函数。
     * @param io_context Boost.Asio的io_context，用于所有同步网络操作。
     * @details： RemoteClient 使用同步IO，以便简化 Repository 中 push/fetch 的流程。
     */
    explicit RemoteClient(boost::asio::io_context& io_context);
    ~RemoteClient();

    // 禁止拷贝和赋值
    RemoteClient(const RemoteClient&) = delete;
    RemoteClient& operator=(const RemoteClient&) = delete;

    bool Connect(const std::string& host, const std::string& port);
    void Disconnect();
    bool IsConnected() const;


    // --- 核心网络操作接口 ---

    bool RegisterUser(const std::string& username, const std::string& password, std::string& out_server_message);
    bool LoginUser(const std::string& username, const std::string& password, std::string& out_token, std::string& out_server_message);

    bool TargetRepository(const std::string& token, const std::string& repo_relative_path); // 添加 token 参数

    std::optional<std::map<std::string, std::string>> ListRemoteRefs(const std::string& token); // 添加 token 参数


    uint16_t GetObject(const std::string& token, const std::string& object_hash, // 添加 token 参数
                       std::string& out_received_object_hash_from_server,
                       std::vector<char>& out_object_raw_content);
    bool CheckObjects(const std::string& token, const std::vector<std::string>& object_hashes_to_check, // 添加 token 参数
                      std::vector<ObjectExistenceStatus>& out_existence_results);
    bool PutObject(const std::string& token, const std::string& object_hash, const char* raw_data, uint32_t data_length); // 添加 token 参数
    bool PutObject(const std::string& token, const std::string& object_hash, const std::vector<char>& raw_object_data_vec); // 添加 token 参数

    uint16_t UpdateRef(const std::string& token, const std::string& ref_full_name, // 添加 token 参数
                       const std::string& new_commit_hash,
                       const std::optional<std::string>& expected_old_commit_hash,
                       bool force_update,
                       std::vector<char>& out_response_body);

private:
    bool SendAndReceive(const SendNode& request_node,
                        uint16_t& out_response_id,
                        std::vector<char>& out_response_body,
                        const std::string& operation_context_for_logging = "");

    bool ReceiveFullMessage(uint16_t& out_id,
                            std::vector<char>& out_body,
                            const std::string& operation_context_for_logging = "");

    // 辅助函数，用于构建带有Token前缀的消息体
    std::vector<char> buildPayloadWithToken(const std::string& token, const char* original_payload, uint32_t original_payload_len);
    std::vector<char> buildPayloadWithToken(const std::string& token, const std::string& original_payload_str);


    boost::asio::io_context& _io_context;
    boost::asio::ip::tcp::socket _socket;
    boost::asio::ip::tcp::resolver _resolver;
    bool _is_connected;
};

}