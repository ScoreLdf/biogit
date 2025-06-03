#pragma once
#include "Singleton.h"
#include "sha1.h"      // 用于密码哈希
#include <filesystem>

namespace Biogit {

// 用于存储用户凭证的内部结构
struct UserRecord {
    std::string username;
    std::string salt;
    std::string hashedPassword; // 密码与盐组合后的哈希值
};

class UserManager : public Singleton<UserManager> {
    friend class Singleton<UserManager>; // 允许 Singleton 访问构造函数

public:
    /**
     * @brief 初始化 UserManager，指定用户数据文件的路径。
     * 如果用户数据文件不存在，会在首次保存时创建。
     * @param data_file_path 用户数据文件的路径。
     */
    void initialize(const std::filesystem::path& data_file_path);

    /**
     * @brief 注册一个新用户。
     * @param username 用户名。
     * @param password 原始密码。
     * @param error_message 如果注册失败，会填充错误信息。
     * @return 如果注册成功，返回 true；否则返回 false。
     */
    bool registerUser(const std::string& username, const std::string& password, std::string& error_message);

    /**
     * @brief 验证用户凭证。
     * @param username 用户名。
     * @param password 原始密码。
     * @param error_message 如果验证失败，会填充错误信息。
     * @return 如果凭证有效，返回 true；否则返回 false。
     */
    bool verifyCredentials(const std::string& username, const std::string& password, std::string& error_message);
    ~UserManager();

private:
    UserManager(); // 私有构造函数，由 Singleton 调用


    /**
     * @brief 从文件加载用户数据到内存。
     * @return 如果加载成功或文件不存在（视为空用户列表），返回 true；如果文件损坏或读取错误，返回 false。
     */
    bool loadUsers();

    /**
     * @brief 将内存中的用户数据保存到文件。
     * @return 如果保存成功，返回 true；否则返回 false。
     */
    bool saveUsers();

    /**
     * @brief 根据用户名查找用户记录。
     * @param username 要查找的用户名。
     * @return 如果找到，返回包含用户记录的 optional；否则返回 std::nullopt。
     */
    std::optional<UserRecord> findUser(const std::string& username) const;

    /**
     * @brief 生成一个随机的盐字符串。
     * @param length 盐的长度。
     * @return 生成的盐字符串。
     */
    std::string generateSalt(size_t length = 16) const;

    /**
     * @brief 对密码和盐进行哈希。
     * @param password 原始密码。
     * @param salt 盐。
     * @return 哈希后的密码字符串。
     */
    std::string hashPassword(const std::string& password, const std::string& salt) const;

    std::vector<UserRecord> m_users;
    std::filesystem::path m_userDataFilePath;
    mutable std::mutex m_usersMutex; // 用于保护对 m_users 的并发访问
    bool m_initialized;
};


class TokenManager : public Singleton<TokenManager> {
    friend class Singleton<TokenManager>;

public:
    /**
     * @brief 初始化 TokenManager，设置服务器的密钥。
     * 这个密钥必须保密，并且在服务器重启后保持一致才能验证旧的 Token。
     * @param server_secret 服务器用于签名和验证 Token 的密钥。
     */
    void initialize(const std::string& server_secret);

    /**
     * @brief 为指定用户生成一个 Token。
     * @param username 要为其生成 Token 的用户名。
     * @param duration_seconds Token 的有效时长（从现在开始，单位：秒）。
     * @return 生成的 Token 字符串；如果 TokenManager 未初始化，则返回空字符串。
     */
    std::string generateToken(const std::string& username, long long duration_seconds = 3600); // 默认1小时有效期

    /**
     * @brief 验证一个 Token 的有效性。
     * 会检查签名是否正确以及 Token 是否过期。
     * @param token 要验证的 Token 字符串。
     * @param out_username 如果 Token 有效，将通过此参数返回关联的用户名。
     * @return 如果 Token 有效且未过期，返回 true；否则返回 false。
     */
    bool validateToken(const std::string& token, std::string& out_username);

    ~TokenManager();
private:
    TokenManager(); // 私有构造函数


    // 简单使用SHA1(data + secret)
    std::string createSignature(const std::string& data_payload) const;
    bool verifySignature(const std::string& data_payload, const std::string& signature) const;


    std::string m_serverSecret;
    bool m_initialized;
};


} // namespace Biogit




