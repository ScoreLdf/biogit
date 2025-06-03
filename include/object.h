#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace Biogit {
using std::string;
using std::vector;
using std::unordered_map;

class Blob {
public:
    vector<std::byte> content; // 文件内容的原始字节

    /**
     * @brief 从原始字节数据构造 Blob 对象。
     * @param data 文件的字节内容。
     */
    explicit Blob(std::vector<std::byte> data);

    /**
     * @brief 从文本字符串构造 Blob 对象
     * @param text_data 文件的文本内容。
     */
    explicit Blob(const std::string& text_data);


    // --- 类型与序列化 ---
    /**
     * @brief 返回此对象类型的 Git 字符串标识符 ("blob")。
     */
    static const std::string& type_str();

    /**
     * @brief 将 Blob 对象序列化为 Git 对象格式的字节流。
     * 格式："blob <content.size()>\0<content_data>"
     * @return 包含序列化数据的字节向量。
     */
    std::vector<std::byte> serialize() const;

    /**
     * @brief 从已去除 Git 对象头部的原始内容数据反序列化 Blob 对象。
     * @param raw_content_data 仅包含文件原始内容的字节向量。
     * @return 如果成功，返回 Blob 对象；否则返回 std::nullopt
     */
    static std::optional<Blob> deserialize(const std::vector<std::byte>& raw_content_data);


    /**
     * @brief 将当前的 Blob 对象保存到对象库中。
     * 它会序列化对象，计算哈希，然后将序列化的数据写入文件。
     * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
     * @return 对象的 SHA-1 哈希值 (40字符的十六进制字符串)；如果保存失败则返回 std::nullopt。
     */
    std::optional<std::string> save(const std::filesystem::path& objects_dir_path) const;

    /**
     * @brief 从对象库中根据 SHA-1 哈希加载 Blob 对象。
     * @param hash_hex 要加载的对象的40字符十六进制 SHA-1 哈希。
     * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
     * @return 如果成功加载，返回 Blob 对象；否则返回 std::nullopt。
     */
    static std::optional<Blob> load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path);


    /**
     * @brief 将 Blob 的内容作为 std::string 返回。
     * 如果内容不是有效的 UTF-8 文本，结果可能无意义或包含乱码。
     */
    std::string get_content_as_string() const;

};


// --- TreeEntry 结构体 ---
// 代表 Tree 对象中的单个条目（文件或子目录）
struct TreeEntry {
    std::string mode;        // 文件/目录模式 (例如："100644", "040000")
    std::string name;        // 文件或目录的名称
    std::string sha1_hash_hex; // 指向的 Blob 或 Tree 对象的40字符十六进制SHA-1哈希

    // 构造函数
    TreeEntry(std::string m, std::string n, std::string hash_hex_str);

    // 辅助函数：检查此条目是否代表一个目录
    bool is_directory() const;

    // 将此单个条目序列化为其字节格式
    // 格式: "<模式> <名称>\0<40字符的十六进制SHA1哈希>"
    std::vector<std::byte> serialize() const;
};


// --- Tree 类 ---
// 代表一个 Git Tree 对象（一个目录的快照）
class Tree {
public:
    std::vector<TreeEntry> entries; // 此树中包含的条目列表

    Tree() = default;
    static const std::string& type_str();


    // 向树中添加一个条目。条目将按照 Git 的规则（调整后适应十六进制哈希）保持排序。
    void add_entry(const TreeEntry& entry);
    void add_entry(const std::string& mode, const std::string& name, const std::string& sha1_hash_hex);


    /**
     * @brief 将 Tree 对象序列化为 Git 对象格式的字节流。
     * 格式："tree <content.size()>\0<content_data>"
     *         <content_data>格式：<模式> <名称>\0<哈希>
     * @return 包含序列化数据的字节向量。
     */
    std::vector<std::byte> serialize() const;

    /**
     * @brief 从已去除 Git 对象头部的原始内容数据反序列化 Tree 对象。
     * @param raw_content_data 仅包含文件原始内容的字节向量。
     * @return 如果成功，返回 Tree 对象；否则返回 std::nullopt
     */
    static std::optional<Tree> deserialize(const std::vector<std::byte>& raw_content_data);


    /**
     * @brief 将当前的 Tree 对象保存到对象库中。
     * 它会序列化对象，计算哈希，然后将序列化的数据写入文件。
     * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
     * @return 对象的 SHA-1 哈希值 (40字符的十六进制字符串)；如果保存失败则返回 std::nullopt。
     */
    std::optional<std::string> save(const std::filesystem::path& objects_dir_path) const;

    /**
      * @brief 从对象库中根据 SHA-1 哈希加载 Tree 对象。
      * @param hash_hex 要加载的对象的40字符十六进制 SHA-1 哈希。
      * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
      * @return 如果成功加载，返回 Tree 对象；否则返回 std::nullopt。
      */
    static std::optional<Tree> load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path);

private:
    /**
      * @brief Tree 对象内部的条目 按照特定的规则进行排序
      *           目录添加/ 用于区分文件
      */
    void sort_entries();
};

// --- PersonTimestamp 结构体 ---
// 用于表示作者和提交者信息，包括姓名、邮箱、时间戳和时区偏移
struct PersonTimestamp {
    std::string name;
    std::string email;
    std::chrono::system_clock::time_point timestamp; // 使用 system_clock::time_point 存储时间
    std::string timezone_offset; // 例如 "+0800" 或 "-0500"

    // 默认构造函数
    PersonTimestamp() = default;

    // 带参数的构造函数
    PersonTimestamp(std::string n, std::string e, const std::chrono::system_clock::time_point& ts, std::string tz_offset);

    /**
     * @brief 将 PersonTimestamp 格式化为 Git Commit 对象中所需的字符串
     * 格式: "<姓名> <<邮箱>> <Unix时间戳秒数> <时区偏移>"
     */
    std::string format_for_commit() const;

    /**
     * @brief 从 Commit 对象中的一行内容（移除了 "author " 或 "committer " 前缀后）解析 PersonTimestamp
     */
    static std::optional<PersonTimestamp> parse_from_line_content(const std::string& line_content);
};


// --- Commit 类 ---
// 代表一个 Git Commit 对象
class Commit {
public:
    std::string tree_hash_hex;                  // 指向的根 Tree 对象的40字符十六进制SHA-1哈希
    std::vector<std::string> parent_hashes_hex; // 父 Commit 的40字符十六进制SHA-1哈希列表
                                                //     合并提交会有多个父提交
    PersonTimestamp author;                     // 作者信息
    PersonTimestamp committer;                  // 提交者信息
    std::string message;                        // 提交信息

    // 默认构造函数
    Commit() = default;

    // 获取对象类型的字符串标识符
    static const std::string& type_str();

    /**
     * @brief 将 Commit 对象序列化为 Git 对象格式的字节流。
     * 格式："commit <content.size()>\0<content_data>"
     *         <content_data>格式：tree treehash
     *                            parent parent_hex...
     *                            author
     *                            commit
     *                            message
     * @return 包含序列化数据的字节向量。
     */
    std::vector<std::byte> serialize() const;

    /**
     * @brief 从已去除 Git 对象头部的原始内容数据反序列化 Commit 对象。
     * @param raw_content_data 仅包含文件原始内容的字节向量。
     * @return 如果成功，返回 Commit 对象；否则返回 std::nullopt
     */
    static std::optional<Commit> deserialize(const std::vector<std::byte>& raw_content_data);

    /**
     * @brief 将当前的 Commit 对象保存到对象库中。
     * 它会序列化对象，计算哈希，然后将序列化的数据写入文件。
     * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
     * @return 对象的 SHA-1 哈希值 (40字符的十六进制字符串)；如果保存失败则返回 std::nullopt。
     */
    std::optional<std::string> save(const std::filesystem::path& objects_dir_path) const;

    /**
     * @brief 从对象库中根据 SHA-1 哈希加载 Commit 对象。
     * @param hash_hex 要加载的对象的40字符十六进制 SHA-1 哈希。
     * @param objects_dir_path BioGit 仓库中 'objects' 目录的路径。
     * @return 如果成功加载，返回 Commit 对象；否则返回 std::nullopt。
     */
    static std::optional<Commit> load_by_hash(const std::string& hash_hex, const std::filesystem::path& objects_dir_path);
};
}


