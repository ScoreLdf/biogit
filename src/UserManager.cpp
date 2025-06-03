#include "../include/UserManager.h"

#include <fstream>
#include <random>
#include <sstream>

namespace Biogit {

UserManager::UserManager() : m_initialized(false) {
    // 实际的初始化（特别是文件路径的设置和加载）将在 initialize() 中进行
}

UserManager::~UserManager() {
    // 如果有需要清理的资源，可以在这里处理
}

void UserManager::initialize(const std::filesystem::path& data_file_path) {
    std::lock_guard<std::mutex> lock(m_usersMutex);
    if (m_initialized) {
        return; // 防止重复初始化
    }
    m_userDataFilePath = data_file_path;
    std::filesystem::path parent_dir = m_userDataFilePath.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(parent_dir, ec)) {
            std::cerr << "UserManager Error: Failed to create directory for user data file: "
                      << parent_dir.string() << " - " << ec.message() << std::endl;
            // 初始化失败，但我们不在这里抛出异常，loadUsers会处理文件不存在的情况
        }
    }
    loadUsers();
    m_initialized = true;
    std::cout << "UserManager initialized. User data file: " << m_userDataFilePath.string() << ". Loaded " << m_users.size() << " users." << std::endl;
}

bool UserManager::loadUsers() {
    // 调用此函数前，调用者应确保已获取 m_usersMutex
    if (m_userDataFilePath.empty()) {
         std::cerr << "UserManager Error: User data file path not set." << std::endl;
        return false; // 或者抛出异常
    }

    m_users.clear();
    std::ifstream ifs(m_userDataFilePath);
    if (!ifs.is_open()) {
        if (std::filesystem::exists(m_userDataFilePath)) { // 文件存在但无法打开
            std::cerr << "UserManager Warning: Could not open user data file: " << m_userDataFilePath.string() << ". Assuming no users." << std::endl;
        } else { // 文件不存在，是正常情况（例如首次启动）
            std::cout << "UserManager Info: User data file not found: " << m_userDataFilePath.string() << ". Will be created on first save. Assuming no users." << std::endl;
        }
        return true; // 文件不存在或打不开，视为空用户列表，不认为是致命错误
    }

    std::string line;
    int line_num = 0;
    while (std::getline(ifs, line)) {
        line_num++;
        std::stringstream ss(line);
        std::string username, salt, hashedPassword;

        if (std::getline(ss, username, ':') &&
            std::getline(ss, salt, ':') &&
            std::getline(ss, hashedPassword)) {
            m_users.push_back({username, salt, hashedPassword});
        } else {
            std::cerr << "UserManager Warning: Malformed line " << line_num << " in user data file: " << m_userDataFilePath.string() << std::endl;
        }
    }
    ifs.close();
    return true;
}

bool UserManager::saveUsers() {
    // 调用此函数前，调用者应确保已获取 m_usersMutex
     if (m_userDataFilePath.empty()) {
         std::cerr << "UserManager Error: User data file path not set. Cannot save users." << std::endl;
        return false;
    }

    std::ofstream ofs(m_userDataFilePath, std::ios::trunc); // 覆盖写入
    if (!ofs.is_open()) {
        std::cerr << "UserManager Error: Could not open user data file for writing: " << m_userDataFilePath.string() << std::endl;
        return false;
    }

    for (const auto& user_record : m_users) {
        ofs << user_record.username << ":"
            << user_record.salt << ":"
            << user_record.hashedPassword << std::endl;
    }

    bool success = ofs.good(); // 检查写入操作是否成功
    ofs.close();
    if(!success) {
         std::cerr << "UserManager Error: Failed to write all user data to file: " << m_userDataFilePath.string() << std::endl;
    }
    return success;
}

std::string UserManager::generateSalt(size_t length) const {
    const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string salt_str;
    salt_str.reserve(length);
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, charset.length() - 1);
    for (size_t i = 0; i < length; ++i) {
        salt_str += charset[distribution(generator)];
    }
    return salt_str;
}

std::string UserManager::hashPassword(const std::string& password, const std::string& salt) const {
    // TODO 应使用更安全的哈希算法，简化使用SHA1 ，简单的加盐方式：将盐附加到密码后进行哈希
    return SHA1::sha1(password + salt); //
}

std::optional<UserRecord> UserManager::findUser(const std::string& username) const {
    // 调用此函数前，调用者应确保已获取 m_usersMutex (如果是在多线程环境中从外部调用的话)
    // 但 UserManager 的 public 方法会处理锁，所以这里假设锁已被外部 public 方法获取
    auto it = std::find_if(m_users.cbegin(), m_users.cend(),
                           [&username](const UserRecord& record) {
                               return record.username == username;
                           });
    if (it != m_users.cend()) {
        return *it;
    }
    return std::nullopt;
}

bool UserManager::registerUser(const std::string& username, const std::string& password, std::string& error_message) {
    if (!m_initialized) {
        error_message = "UserManager not initialized.";
        std::cerr << "UserManager Error: " << error_message << std::endl;
        return false;
    }
    std::lock_guard<std::mutex> lock(m_usersMutex);

    if (username.empty() || password.empty()) {
        error_message = "Username and password cannot be empty.";
        return false;
    }
    if (username.find(':') != std::string::npos) { // 避免与我们的文件格式冲突
        error_message = "Username cannot contain ':' character.";
        return false;
    }

    if (findUser(username)) {
        error_message = "Username '" + username + "' already exists.";
        return false;
    }

    std::string salt = generateSalt();
    std::string hashed_password = hashPassword(password, salt);

    m_users.push_back({username, salt, hashed_password});

    if (!saveUsers()) {
        error_message = "Failed to save new user to data file.";
        // 回滚添加的用户（可选，但更健壮）
        m_users.pop_back();
        return false;
    }
    error_message = "User registered successfully.";
    return true;
}

bool UserManager::verifyCredentials(const std::string& username, const std::string& password, std::string& error_message) {
    if (!m_initialized) {
        error_message = "UserManager not initialized.";
        std::cerr << "UserManager Error: " << error_message << std::endl;
        return false;
    }
    std::lock_guard<std::mutex> lock(m_usersMutex);

    auto user_opt = findUser(username);
    if (!user_opt) {
        error_message = "Invalid username or password."; // 不明确指出是用户名还是密码错误
        return false;
    }

    const UserRecord& user_record = *user_opt;
    std::string hashed_password_to_check = hashPassword(password, user_record.salt);

    if (hashed_password_to_check == user_record.hashedPassword) {
        error_message = "Login successful.";
        return true;
    }

    error_message = "Invalid username or password.";
    return false;
}

TokenManager::TokenManager() : m_initialized(false) {}

TokenManager::~TokenManager() {}

void TokenManager::initialize(const std::string& server_secret) {
    if (m_initialized) {
        return;
    }
    if (server_secret.empty()) {
        std::cerr << "TokenManager Error: Server secret cannot be empty for initialization!" << std::endl;
        // 实际应用中应该抛出异常或使程序无法启动
        return;
    }
    m_serverSecret = server_secret;
    m_initialized = true;
    std::cout << "TokenManager initialized." << std::endl;
}

std::string TokenManager::createSignature(const std::string& data_payload) const {
    // 简单的签名方式：SHA1(payload + secret)
    // 更安全的做法是使用 HMAC-SHA1 或 HMAC-SHA256
    return SHA1::sha1(data_payload + m_serverSecret); //
}

bool TokenManager::verifySignature(const std::string& data_payload, const std::string& received_signature) const {
    return createSignature(data_payload) == received_signature;
}

std::string TokenManager::generateToken(const std::string& username, long long duration_seconds) {
    if (!m_initialized) {
        std::cerr << "TokenManager Error: Not initialized. Cannot generate token." << std::endl;
        return "";
    }
    if (username.empty() || username.find(':') != std::string::npos) {
         std::cerr << "TokenManager Error: Invalid username for token generation (empty or contains ':')." << std::endl;
        return "";
    }

    auto now = std::chrono::system_clock::now();
    auto expiry_time_point = now + std::chrono::seconds(duration_seconds);
    long long expiry_timestamp = std::chrono::duration_cast<std::chrono::seconds>(expiry_time_point.time_since_epoch()).count();

    std::stringstream payload_ss;
    payload_ss << username << ":" << expiry_timestamp;
    std::string data_payload = payload_ss.str();

    std::string signature = createSignature(data_payload);

    std::stringstream token_ss;
    token_ss << data_payload << ":" << signature;
    return token_ss.str(); // Token 格式: "username:expiry_timestamp:signature"
}

bool TokenManager::validateToken(const std::string& token, std::string& out_username) {
    if (!m_initialized) {
        std::cerr << "TokenManager Error: Not initialized. Cannot validate token." << std::endl;
        return false;
    }
    out_username.clear();

    std::stringstream token_ss(token);
    std::string username_part;
    std::string expiry_timestamp_str_part;
    std::string signature_part;

    // 解析 Token: "username:expiry_timestamp:signature"
    if (std::getline(token_ss, username_part, ':') &&
        std::getline(token_ss, expiry_timestamp_str_part, ':') &&
        std::getline(token_ss, signature_part)) {

        // 1. 验证签名
        std::string data_payload_to_verify = username_part + ":" + expiry_timestamp_str_part;
        if (!verifySignature(data_payload_to_verify, signature_part)) {
            // std::cerr << "TokenManager Debug: Invalid signature for token." << std::endl;
            return false; // 签名不匹配
        }

        // 2. 验证过期时间
        long long expiry_timestamp;
        try {
            expiry_timestamp = std::stoll(expiry_timestamp_str_part);
        } catch (const std::exception& e) {
            std::cerr << "TokenManager Error: Invalid expiry timestamp format in token: " << e.what() << std::endl;
            return false; // 时间戳格式错误
        }

        auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now_timestamp > expiry_timestamp) {
            // std::cerr << "TokenManager Debug: Token expired." << std::endl;
            return false; // Token 已过期
        }

        // 3. 如果都通过，Token 有效
        out_username = username_part;
        return true;
    }

    // std::cerr << "TokenManager Debug: Malformed token string." << std::endl;
    return false; // Token 格式不正确
}




} // namespace Biogit