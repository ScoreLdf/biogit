#pragma once
#include <string>
#include <unordered_set>
#include <filesystem>
#include <chrono>

namespace Biogit {
using std::string;
using std::unordered_map;
using std::unordered_set;

// 结构体，用于表示索引中的单个条目
struct IndexEntry {
    std::string mode;                   // 文件模式, e.g., "100644" (普通文件), "100755" (可执行)
    std::string blob_hash_hex;          // 对应 Blob 对象的40字符十六进制SHA-1哈希
    std::chrono::system_clock::time_point mtime; // 文件最后修改时间
    uint64_t file_size;                 // 文件大小 (字节)
    std::filesystem::path file_path;    // 文件路径 (相对于工作树根目录，已规范化)

    // 用于 std::sort 和 std::vector 操作的比较运算符
    bool operator<(const IndexEntry& other) const {
        return file_path < other.file_path;
    }

    /**
     * @brief 将 索引中的单个条目 格式化为 字符串
     * 格式: <模式> <Blob哈希> <mtime秒> <mtime纳秒> <文件大小> <文件路径(相对于工作树根目录)>
     */
    std::string format_for_file() const;

    /**
     * @brief 从索引文件的一行字符串解析出 IndexEntry
     */
    static std::optional<IndexEntry> parse_from_line(const std::string& line);
};


// Index保存的都是相对于工作树根目录的文件路径
class Index {
private:
    std::filesystem::path index_file_path_; // .biogit/index 文件的完整路径
    std::vector<IndexEntry> entries_;       // 内存中存储的索引条目
    bool loaded_ = false;                   // 标记索引是否已从磁盘加载

    // 确保内部条目按文件路径排序
    void sort_entries_();

public:
    /**
     * @brief 构造 Index 对象。
     * @param biogit_dir_path 指向 .biogit 目录的路径。
     */
    explicit Index(const std::filesystem::path& biogit_dir_path);

    /**
     * @brief 从磁盘上的 .biogit/index 文件加载索引条目到内存。
     * 如果索引文件不存在，则 entries_ 保持为空，这被认为是成功加载（空索引）。
     * @return 如果加载过程中发生I/O错误（非文件不存在），则返回 false。
     */
    bool load();

    /**
     * @brief 将内存中的索引条目写回到磁盘上的 .biogit/index 文件。
     * 所有修改 entries_的都sort 所以write内部不需要sort
     * @return 如果写入成功，返回 true；否则返回 false。
     */
    bool write() const;

    /**
     * @brief 向索引中添加或更新一个文件条目。
     * @param relative_path 规范化的、相对于工作树根目录的文件路径。
     * @param blob_hash_hex 该文件内容对应的 Blob 对象的十六进制哈希。
     * @param file_mode 文件模式 (例如 "100644")。
     * @param mtime 文件的最后修改时间。
     * @param file_size 文件的大小（字节）。
     * @return 如果成功添加或更新条目到内存列表，返回 true。
     * 注意：此操作仅修改内存中的索引，需要调用 write() 来持久化。
     */
    bool add_or_update_entry(const std::filesystem::path& relative_path,
                             const std::string& blob_hash_hex,
                             const std::string& file_mode,
                             const std::chrono::system_clock::time_point& mtime,
                             uint64_t file_size);

    /**
     * @brief 从索引中移除一个文件条目。
     * @param relative_path 要移除的文件的路径 (相对于工作树根目录，已规范化)。
     * @return 如果找到并移除了条目，返回 true；否则返回 false。
     * 注意：此操作仅修改内存中的索引，需要调用 write() 来持久化。
     */
    bool remove_entry(const std::filesystem::path& relative_path);

    /**
      * @brief 根据相对路径获取索引中的一个条目。
      * @param relative_path 要查找的文件的路径 (相对于工作树根目录，已规范化)。
      * @return 如果找到，返回 IndexEntry；否则返回 std::nullopt。
      * 调用者应确保索引已加载。
      */
    std::optional<IndexEntry> get_entry(const std::filesystem::path& relative_path) const;

    /**
     * @brief 获取所有索引条目的常量引用。
     * 调用者应确保索引已加载。
     * @return 指向内存中索引条目向量的常量引用。
     */
    const std::vector<IndexEntry>& get_all_entries() const;

    /**
     * @brief 清空内存中的所有索引条目。
     * 注意：此操作仅修改内存中的索引，需要调用 write() 来持久化一个空的索引文件。
     */
    void clear_in_memory();


    /**
     * @brief 检查索引是否已从磁盘加载过。
     */
    bool is_loaded() const { return loaded_; }
};





}




