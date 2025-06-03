#include "../include/object.h"
#include "../include/sha1.h"
#include <iostream>
#include <fstream>
#include <sstream>
namespace Biogit {
/**
 * @brief 内部辅助函数：用于从文件中读取对象头部和内容
 * @param file_path filesystem::path类型
 * @return 返回 {对象类型字符串, 对象大小, 对象内容字节流 (不含头部)} 如果解析失败或读取错误，返回 std::nullopt
 */
static std::optional<std::tuple<std::string, size_t, std::vector<std::byte>>>
read_and_parse_object_file(const std::filesystem::path& file_path) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        // std::cerr << "错误: 无法打开对象文件 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }

    std::string type_str_read;
    long long content_size_read = -1; // Git 对象大小理论上可以很大
    char ch;

    // 1. 读取类型 (直到空格)
    while (ifs.get(ch) && ch != ' ') {
        type_str_read += ch;
    }
    if (type_str_read.empty() || ch != ' ') { // 确保读到了类型和空格
        // std::cerr << "错误: 读取对象类型失败或格式错误于 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }

    // 2. 读取大小 (直到 '\0')
    std::string size_str_read;
    while (ifs.get(ch) && ch != '\0') {
        size_str_read += ch;
    }
    if (size_str_read.empty() || ch != '\0') { // 确保读到了大小和空终止符
        // std::cerr << "错误: 读取对象大小失败或格式错误于 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }

    try {
        content_size_read = std::stoll(size_str_read); // 使用 stoll 处理可能较大的size
    } catch (const std::exception& e) {
        // std::cerr << "错误: 转换对象大小失败于 '" << file_path.string() << "': " << e.what() << std::endl;
        return std::nullopt;
    }

    if (content_size_read < 0) {
        // std::cerr << "错误: 无效的对象大小 " << content_size_read << " 于 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }

    // 3. 读取实际内容数据
    std::vector<std::byte> content_data(static_cast<size_t>(content_size_read));
    if (content_size_read > 0) { // 只有当大小大于0时才读取
        ifs.read(reinterpret_cast<char*>(content_data.data()), content_size_read);
        if (ifs.gcount() != content_size_read) {
            // std::cerr << "错误: 读取对象内容不足，期望 " << content_size_read
            //           << " 字节，实际读取 " << ifs.gcount() << " 字节于 '" << file_path.string() << "'" << std::endl;
            return std::nullopt; // 未能读取完整内容
        }
    }

    return std::make_tuple(type_str_read, static_cast<size_t>(content_size_read), content_data);
}



// --- 构造函数实现 ---
Blob::Blob(std::vector<std::byte> data) : content(std::move(data)) {}

Blob::Blob(const std::string& text_data) {
    content.resize(text_data.length());
    std::transform(text_data.begin(), text_data.end(), content.begin(), [](char c) {
        return static_cast<std::byte>(c);
    });
}

// --- 类型与序列化实现 ---
const std::string& Blob::type_str() {
    static const std::string type = "blob";
    return type;
}

std::vector<std::byte> Blob::serialize() const {
    // 构造头部: "blob <size>\0"
    std::string header_str = type_str() + " " + std::to_string(content.size()) + '\0';

    std::vector<std::byte> serialized_data;
    serialized_data.reserve(header_str.length() + content.size());
    for (char ch : header_str) {
        serialized_data.push_back(static_cast<std::byte>(ch));
    }
    serialized_data.insert(serialized_data.end(), content.begin(), content.end());

    return serialized_data;
}

std::optional<Blob> Blob::deserialize(const std::vector<std::byte>& raw_content_data) {
    // 对于 Blob，反序列化非常直接：原始内容数据就是 Blob 的内容。
    // 这里 raw_content_data 应该是已经去除了 "blob <size>\0" 头之后的部分。
    return Blob(raw_content_data);
}



std::optional<std::string> Blob::save(const std::filesystem::path& objects_dir_path) const {
    // 1. 序列化 Blob 对象 (获取 "blob <size>\0<content>" 格式的字节流)
    std::vector<std::byte> serialized_data = this->serialize();

    // 2. 计算序列化数据的 SHA-1 哈希值 (作为文件名)
    //    确保 SHA1::calculate 接受 vector<byte> 并返回十六进制字符串
    std::string hash_hex = SHA1::sha1(serialized_data);
    if (hash_hex.empty() || hash_hex.length() != 40) { // 基本的哈希有效性检查
        std::cerr << "错误: 计算 SHA1 哈希失败或格式不正确。" << std::endl;
        return std::nullopt;
    }

    // 3. 构建对象文件的存储路径 (例如: objects/ab/cdef...)
    std::filesystem::path dir_part = objects_dir_path / hash_hex.substr(0, 2);
    std::filesystem::path file_part = dir_part / hash_hex.substr(2);

    std::error_code ec;
    // 4. 创建对象子目录 (如果不存在)
    if (!std::filesystem::exists(dir_part, ec)) {
        if (!std::filesystem::create_directories(dir_part, ec)) {
            std::cerr << "错误: 无法创建对象子目录 '" << dir_part.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
    } else if (!std::filesystem::is_directory(dir_part, ec)) {
         std::cerr << "错误: 对象路径的目录部分 '" << dir_part.string() << "' 已存在但不是目录。" << std::endl;
        return std::nullopt;
    }


    // 5. 将序列化的数据写入文件 (如果文件已存在，则覆盖)
    if (std::filesystem::exists(file_part, ec)) {
        std::cout << "提示: 对象 " << hash_hex << " 已存在，无需保存。" << std::endl;
        return hash_hex; // 对象已存在，返回其哈希
    }

    std::ofstream ofs(file_part, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开或创建对象文件 '" << file_part.string() << "' 进行写入。" << std::endl;
        return std::nullopt;
    }

    ofs.write(reinterpret_cast<const char*>(serialized_data.data()), serialized_data.size());
    if (!ofs.good()) { // 检查写入操作是否成功
        std::cerr << "错误: 写入对象文件 '" << file_part.string() << "' 失败。" << std::endl;
        ofs.close(); // 尝试关闭
        std::filesystem::remove(file_part, ec); // 尝试删除写入失败的文件
        return std::nullopt;
    }
    ofs.close();

    return hash_hex; // 返回计算出的哈希值
}

std::optional<Blob> Blob::load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path) {
    if (hash_hex.length() != 40) {
        // std::cerr << "错误: 无效的 SHA1 哈希长度: " << hash_hex << std::endl;
        return std::nullopt;
    }

    std::filesystem::path file_path = objects_dir_path / hash_hex.substr(0, 2) / hash_hex.substr(2);

    if (!std::filesystem::exists(file_path)) {
        // std::cerr << "错误: 对象文件不存在 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }

    auto parsed_result = read_and_parse_object_file(file_path);
    if (!parsed_result) {
        // read_and_parse_object_file 内部会打印更详细的错误
        return std::nullopt;
    }

    const auto& [type_str_read, content_size_read, raw_content_data] = *parsed_result;

    // 验证对象类型是否为 "blob"
    if (type_str_read != Blob::type_str()) {
        std::cerr << "错误: 对象类型不匹配于 '" << file_path.string()
                  << "'. 期望 '" << Blob::type_str() << "', 实际为 '" << type_str_read << "'." << std::endl;
        return std::nullopt;
    }

    // 重新构建头部和内容，计算哈希，与传入的 hash_hex 比较
    std::string header_for_verify = Blob::type_str() + " " + std::to_string(raw_content_data.size()) + '\0';
    std::vector<std::byte> data_for_verify;
    data_for_verify.reserve(header_for_verify.length() + raw_content_data.size());
    for (char ch_h : header_for_verify) { data_for_verify.push_back(static_cast<std::byte>(ch_h)); }
    data_for_verify.insert(data_for_verify.end(), raw_content_data.begin(), raw_content_data.end());

    std::string calculated_hash = SHA1::sha1(data_for_verify);
    if (calculated_hash != hash_hex) {
        std::cerr << "错误: 对象数据损坏或哈希不匹配于 '" << file_path.string()
                  << "'. 文件哈希: " << calculated_hash << ", 期望哈希: " << hash_hex << std::endl;
        return std::nullopt;
    }

    // 使用静态 deserialize 方法创建 Blob 对象
    return Blob::deserialize(raw_content_data);
}


std::string Blob::get_content_as_string() const {
    if (content.empty()) {
        return "";
    }
    // 假设内容是有效的文本，如果包含非文本字节，行为未定义
    return std::string(reinterpret_cast<const char*>(content.data()), content.size());
}



// --- TreeEntry 实现 ---
TreeEntry::TreeEntry(std::string m, std::string n, std::string hash_hex_str)
    : mode(std::move(m)), name(std::move(n)), sha1_hash_hex(std::move(hash_hex_str)) {
    // 可以添加对 sha1_hash_hex 长度和格式的验证
    if (this->sha1_hash_hex.length() != 40) {
        // 根据你的错误处理策略，可以抛出异常或标记为无效
        std::cerr << "警告: TreeEntry 创建时哈希长度不为40: " << this->sha1_hash_hex << std::endl;
    }
}

bool TreeEntry::is_directory() const {
    return mode == "040000";
}

std::vector<std::byte> TreeEntry::serialize() const {
    std::vector<std::byte> entry_data;
    // 追加模式
    for (char ch : mode) { entry_data.push_back(static_cast<std::byte>(ch)); }
    entry_data.push_back(static_cast<std::byte>(' ')); // 空格
    // 追加名称
    for (char ch : name) { entry_data.push_back(static_cast<std::byte>(ch)); }
    entry_data.push_back(static_cast<std::byte>('\0')); // 名称后的空终止符
    // 追加40字符的十六进制SHA1哈希
    for (char ch : sha1_hash_hex) { entry_data.push_back(static_cast<std::byte>(ch)); }
    return entry_data;
}


// --- Tree 实现 ---

const std::string& Tree::type_str() {
    static const std::string type = "tree";
    return type;
}

void Tree::sort_entries() {
    std::sort(entries.begin(), entries.end(), [](const TreeEntry& a, const TreeEntry& b) {
        std::string name_a_for_sort = a.name;
        if (a.is_directory()) {
            name_a_for_sort += '/';
        }
        std::string name_b_for_sort = b.name;
        if (b.is_directory()) {
            name_b_for_sort += '/';
        }
        return name_a_for_sort < name_b_for_sort;
    });
}

void Tree::add_entry(const TreeEntry& entry) {
    if (entry.sha1_hash_hex.length() != 40) { // 基本验证
        std::cerr << "错误: 尝试添加的 TreeEntry 哈希无效: " << entry.name << std::endl;
        return; // 或者抛出异常
    }
    entries.push_back(entry);
    sort_entries();
}

void Tree::add_entry(const std::string& mode, const std::string& name, const std::string& sha1_hash_hex) {
    if (sha1_hash_hex.length() != 40) { // 基本验证
        std::cerr << "错误: 尝试添加的 TreeEntry 哈希无效: " << name << std::endl;
        return; // 或者抛出异常
    }
    add_entry(TreeEntry(mode, name, sha1_hash_hex));
}

std::vector<std::byte> Tree::serialize() const {
    std::vector<std::byte> content_data;
    for (const auto& entry : entries) { // entries 应该已经是排序好的
        std::vector<std::byte> entry_bytes = entry.serialize();
        content_data.insert(content_data.end(), entry_bytes.begin(), entry_bytes.end());
    }

    std::string header_str = type_str() + " " + std::to_string(content_data.size()) + '\0';
    std::vector<std::byte> serialized_data;
    serialized_data.reserve(header_str.length() + content_data.size());

    for (char ch : header_str) {
        serialized_data.push_back(static_cast<std::byte>(ch));
    }
    serialized_data.insert(serialized_data.end(), content_data.begin(), content_data.end());
    return serialized_data;
}

std::optional<Tree> Tree::deserialize(const std::vector<std::byte>& raw_content_data) {
    Tree tree;
    size_t current_pos = 0;

    while (current_pos < raw_content_data.size()) {
        // 1. 解析模式 (mode)
        std::string mode_str;
        size_t parse_ptr = current_pos;
        while (parse_ptr < raw_content_data.size() && raw_content_data[parse_ptr] != static_cast<std::byte>(' ')) {
            mode_str += static_cast<char>(raw_content_data[parse_ptr]);
            parse_ptr++;
        }
        if (parse_ptr >= raw_content_data.size() || raw_content_data[parse_ptr] != static_cast<std::byte>(' ')) {
            return std::nullopt;
        }
        current_pos = parse_ptr + 1; // 跳过空格

        // 2. 解析名称 (name)
        std::string name_str;
        parse_ptr = current_pos;
        while (parse_ptr < raw_content_data.size() && raw_content_data[parse_ptr] != static_cast<std::byte>('\0')) {
            name_str += static_cast<char>(raw_content_data[parse_ptr]);
            parse_ptr++;
        }
        if (parse_ptr >= raw_content_data.size() || raw_content_data[parse_ptr] != static_cast<std::byte>('\0')) {
            return std::nullopt;
        }
        current_pos = parse_ptr + 1; // 跳过 '\0'

        // 3. 解析40字符的十六进制SHA1哈希
        if (current_pos + 40 > raw_content_data.size()) {
            return std::nullopt; // 数据不足
        }
        std::string sha1_hex_str;
        sha1_hex_str.reserve(40);
        for (size_t i = 0; i < 40; ++i) {
            sha1_hex_str += static_cast<char>(raw_content_data[current_pos + i]);
        }
        current_pos += 40;

        // 反序列化时，条目已经是正确顺序，直接添加到列表
        tree.entries.emplace_back(mode_str, name_str, sha1_hex_str);
    }
    return tree;
}




std::optional<std::string> Tree::save(const std::filesystem::path& objects_dir_path) const {
    std::vector<std::byte> serialized_data = this->serialize();
    std::string hash_hex = SHA1::sha1(serialized_data); // 你的SHA1函数返回十六进制字符串

    if (hash_hex.empty() || hash_hex.length() != 40) {
        std::cerr << "错误: Tree对象计算SHA1哈希失败或格式不正确。" << std::endl;
        return std::nullopt;
    }

    std::filesystem::path dir_part = objects_dir_path / hash_hex.substr(0, 2);
    std::filesystem::path file_part = dir_part / hash_hex.substr(2);
    std::error_code ec;

    if (!std::filesystem::exists(dir_part, ec)) {
        if (!std::filesystem::create_directories(dir_part, ec)) {
            std::cerr << "错误: 无法创建对象子目录 '" << dir_part.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
    } else if (!std::filesystem::is_directory(dir_part, ec)) {
         std::cerr << "错误: 对象路径的目录部分 '" << dir_part.string() << "' 已存在但不是目录。" << std::endl;
        return std::nullopt;
    }

    if (std::filesystem::exists(file_part, ec)) {
        return hash_hex; // 对象已存在
    }

    std::ofstream ofs(file_part, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开或创建对象文件 '" << file_part.string() << "' 进行写入。" << std::endl;
        return std::nullopt;
    }
    ofs.write(reinterpret_cast<const char*>(serialized_data.data()), serialized_data.size());
    if (!ofs.good()) {
        std::cerr << "错误: 写入对象文件 '" << file_part.string() << "' 失败。" << std::endl;
        ofs.close();
        return std::nullopt;
    }
    ofs.close();
    return hash_hex;
}

std::optional<Tree> Tree::load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path) {
    if (hash_hex.length() != 40) { return std::nullopt; }

    std::filesystem::path file_path = objects_dir_path / hash_hex.substr(0, 2) / hash_hex.substr(2);
    if (!std::filesystem::exists(file_path)) { return std::nullopt; }

    auto parsed_result = read_and_parse_object_file(file_path);
    if (!parsed_result) { return std::nullopt; }

    const auto& [type_str_read, content_size_read, raw_content_data] = *parsed_result;

    if (type_str_read != Tree::type_str()) {
        std::cerr << "错误: 对象类型不匹配于 '" << file_path.string()
                  << "'. 期望 '" << Tree::type_str() << "', 实际为 '" << type_str_read << "'." << std::endl;
        return std::nullopt;
    }

    // 可选但推荐: 验证数据完整性
    std::string header_for_verify = Tree::type_str() + " " + std::to_string(raw_content_data.size()) + '\0';
    std::vector<std::byte> data_for_verify;
    for (char ch_h : header_for_verify) { data_for_verify.push_back(static_cast<std::byte>(ch_h)); }
    data_for_verify.insert(data_for_verify.end(), raw_content_data.begin(), raw_content_data.end());

    std::string calculated_hash = SHA1::sha1(data_for_verify);
    if (calculated_hash != hash_hex) {
        std::cerr << "错误: Tree对象数据损坏或哈希不匹配于 '" << file_path.string()
                  << "'. 文件哈希: " << calculated_hash << ", 期望哈希: " << hash_hex << std::endl;
        return std::nullopt;
    }

    return Tree::deserialize(raw_content_data);
}



// --- PersonTimestamp 实现 ---
PersonTimestamp::PersonTimestamp(std::string n, std::string e,
                                 const std::chrono::system_clock::time_point& ts, std::string tz_offset)
    : name(std::move(n)), email(std::move(e)), timestamp(ts), timezone_offset(std::move(tz_offset)) {}

std::string PersonTimestamp::format_for_commit() const {
    // 将 std::chrono::system_clock::time_point 转换为 time_t (Unix时间戳秒数)
    std::time_t time_t_stamp = std::chrono::system_clock::to_time_t(timestamp);

    std::ostringstream oss;
    oss << name << " <" << email << "> " << time_t_stamp << " " << timezone_offset;
    return oss.str();
}

std::optional<PersonTimestamp> PersonTimestamp::parse_from_line_content(const std::string& line_content) {
    PersonTimestamp pt;
    std::istringstream iss(line_content);
    std::string part;
    std::vector<std::string> parts;

    // Git 作者/提交者行格式: "Name <email> timestamp timezone"
    // 名字可能包含空格，所以不能简单地用 >> 分割所有部分

    size_t email_start_pos = line_content.find('<');
    size_t email_end_pos = line_content.find('>');

    if (email_start_pos == std::string::npos || email_end_pos == std::string::npos || email_start_pos > email_end_pos) {
        return std::nullopt; // 邮箱格式不正确
    }

    pt.name = line_content.substr(0, email_start_pos);
    // 去除名字末尾的空格
    if (!pt.name.empty() && pt.name.back() == ' ') {
        pt.name.pop_back();
    }
    if (pt.name.empty()) return std::nullopt; // 名字不能为空

    pt.email = line_content.substr(email_start_pos + 1, email_end_pos - email_start_pos - 1);

    // 解析剩余的时间戳和时区
    std::string remaining_part = line_content.substr(email_end_pos + 1);
    std::istringstream remaining_ss(remaining_part);

    long long ts_seconds;
    remaining_ss >> ts_seconds;
    if (remaining_ss.fail()) return std::nullopt; // 时间戳解析失败

    remaining_ss >> pt.timezone_offset;
    if (remaining_ss.fail() || pt.timezone_offset.empty()) return std::nullopt; // 时区解析失败或为空

    pt.timestamp = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(ts_seconds));
    return pt;
}


// --- Commit 实现 ---

const std::string& Commit::type_str() {
    static const std::string type = "commit";
    return type;
}

std::vector<std::byte> Commit::serialize() const {
    std::ostringstream content_oss;

    content_oss << "tree " << tree_hash_hex << "\n";

    for (const auto& parent_hex : parent_hashes_hex) {
        content_oss << "parent " << parent_hex << "\n";
    }

    content_oss << "author " << author.format_for_commit() << "\n";
    content_oss << "committer " << committer.format_for_commit() << "\n";

    content_oss << "\n"; // 元数据和提交信息之间的空行
    content_oss << message; // 提交信息本身

    std::string content_str = content_oss.str();

    // 构建 Git 对象头部
    std::string header_str = type_str() + " " + std::to_string(content_str.length()) + '\0';
    std::vector<std::byte> serialized_data;
    serialized_data.reserve(header_str.length() + content_str.length());

    for (char ch : header_str) {
        serialized_data.push_back(static_cast<std::byte>(ch));
    }
    for (char ch : content_str) {
        serialized_data.push_back(static_cast<std::byte>(ch));
    }

    return serialized_data;
}

std::optional<Commit> Commit::deserialize(const std::vector<std::byte>& raw_content_data) {
    Commit commit;
    // 将原始字节数据转换为字符串进行解析
    std::string content_str(reinterpret_cast<const char*>(raw_content_data.data()), raw_content_data.size());
    std::istringstream iss(content_str);
    std::string line;

    bool in_metadata_section = true;
    std::vector<std::string> message_lines;

    while (std::getline(iss, line)) {
        if (in_metadata_section) {
            if (line.empty()) { // 空行标志着元数据结束，提交信息开始
                in_metadata_section = false;
                continue;
            }

            size_t first_space = line.find(' ');
            if (first_space == std::string::npos) {
                // std::cerr << "错误: Commit元数据行格式无效: " << line << std::endl;
                return std::nullopt; // 格式错误的行
            }

            std::string key = line.substr(0, first_space);
            std::string value = line.substr(first_space + 1);

            if (key == "tree") {
                if (!commit.tree_hash_hex.empty()) return std::nullopt; // tree 只能有一个
                commit.tree_hash_hex = value;
                if (commit.tree_hash_hex.length() != 40) return std::nullopt; // 哈希长度验证
            } else if (key == "parent") {
                if (value.length() != 40) return std::nullopt; // 哈希长度验证
                commit.parent_hashes_hex.push_back(value);
            } else if (key == "author") {
                auto author_opt = PersonTimestamp::parse_from_line_content(value);
                if (!author_opt) return std::nullopt;
                commit.author = *author_opt;
            } else if (key == "committer") {
                auto committer_opt = PersonTimestamp::parse_from_line_content(value);
                if (!committer_opt) return std::nullopt;
                commit.committer = *committer_opt;
            } else {
                std::cerr << "警告: Commit中遇到未知元数据键: " << key << std::endl;
            }
        } else {
            // 空行之后的所有内容都是提交信息的一部分
            message_lines.push_back(line);
        }
    }

    // 组合提交信息行
    if (!message_lines.empty()) {
        commit.message = message_lines[0];
        for (size_t i = 1; i < message_lines.size(); ++i) {
            commit.message += "\n" + message_lines[i];
        }
    }

    // 必须要有 tree
    if (commit.tree_hash_hex.empty()) {
        // std::cerr << "错误: Commit对象缺少tree元数据。" << std::endl;
        return std::nullopt;
    }
    // 验证 author 和 committer 是否被正确解析（通常 parse_from_line_content 会处理）
    if (commit.author.name.empty() || commit.committer.name.empty()) {
        return std::nullopt;
    }


    return commit;
}


std::optional<std::string> Commit::save(const std::filesystem::path& objects_dir_path) const {
    std::vector<std::byte> serialized_data = this->serialize();
    std::string hash_hex = SHA1::sha1(serialized_data);

    if (hash_hex.empty() || hash_hex.length() != 40) {
        std::cerr << "错误: Commit对象计算SHA1哈希失败或格式不正确。" << std::endl;
        return std::nullopt;
    }

    std::filesystem::path dir_part = objects_dir_path / hash_hex.substr(0, 2);
    std::filesystem::path file_part = dir_part / hash_hex.substr(2);
    std::error_code ec;

    if (!std::filesystem::exists(dir_part, ec)) {
        if (!std::filesystem::create_directories(dir_part, ec)) {
            std::cerr << "错误: 无法创建对象子目录 '" << dir_part.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
    } else if (!std::filesystem::is_directory(dir_part, ec)) {
         std::cerr << "错误: 对象路径的目录部分 '" << dir_part.string() << "' 已存在但不是目录。" << std::endl;
        return std::nullopt;
    }

    if (std::filesystem::exists(file_part, ec)) {
        return hash_hex; // 对象已存在
    }

    std::ofstream ofs(file_part, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开或创建对象文件 '" << file_part.string() << "' 进行写入。" << std::endl;
        return std::nullopt;
    }
    ofs.write(reinterpret_cast<const char*>(serialized_data.data()), serialized_data.size());
    if (!ofs.good()) {
        std::cerr << "错误: 写入对象文件 '" << file_part.string() << "' 失败。" << std::endl;
        ofs.close();
        return std::nullopt;
    }
    ofs.close();
    return hash_hex;
}

std::optional<Commit> Commit::load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path) {
    if (hash_hex.length() != 40) { return std::nullopt; }

    std::filesystem::path file_path = objects_dir_path / hash_hex.substr(0, 2) / hash_hex.substr(2);
    if (!std::filesystem::exists(file_path)) { return std::nullopt; }

    auto parsed_result = read_and_parse_object_file(file_path);
    if (!parsed_result) { return std::nullopt; }

    const auto& [type_str_read, content_size_read, raw_content_data] = *parsed_result;

    if (type_str_read != Commit::type_str()) {
        std::cerr << "错误: 对象类型不匹配于 '" << file_path.string()
                  << "'. 期望 '" << Commit::type_str() << "', 实际为 '" << type_str_read << "'." << std::endl;
        return std::nullopt;
    }

    // 可选但推荐: 验证数据完整性
    std::string header_for_verify = Commit::type_str() + " " + std::to_string(raw_content_data.size()) + '\0';
    std::vector<std::byte> data_for_verify;
    for (char ch_h : header_for_verify) { data_for_verify.push_back(static_cast<std::byte>(ch_h)); }
    data_for_verify.insert(data_for_verify.end(), raw_content_data.begin(), raw_content_data.end());

    std::string calculated_hash = SHA1::sha1(data_for_verify);
    if (calculated_hash != hash_hex) {
        std::cerr << "错误: Commit对象数据损坏或哈希不匹配于 '" << file_path.string()
                  << "'. 文件哈希: " << calculated_hash << ", 期望哈希: " << hash_hex << std::endl;
        return std::nullopt;
    }

    return Commit::deserialize(raw_content_data);
}
}

