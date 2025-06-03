#include "../include/Index.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include "Repository.h"

namespace Biogit {

// --- IndexEntry 实现 ---

std::string IndexEntry::format_for_file() const {
    std::ostringstream oss;
    auto mtime_sec_count = std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
    auto mtime_nsec_part = std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count() % 1000000000;

    oss << mode << " "
        << blob_hash_hex << " "
        << mtime_sec_count << " "
        << mtime_nsec_part << " "
        << file_size << " "
        << file_path.generic_string(); // 使用 generic_string 以确保路径分隔符为 '/'
    return oss.str();
}

std::optional<IndexEntry> IndexEntry::parse_from_line(const std::string& line) {
    std::istringstream iss(line);
    IndexEntry entry;
    long long mtime_sec_ll;
    long long mtime_nsec_ll; // 用 long long 读取，然后转 uint32_t

    // 格式: <模式> <Blob哈希> <mtime秒> <mtime纳秒> <文件大小> <文件路径>
    iss >> entry.mode >> entry.blob_hash_hex >> mtime_sec_ll >> mtime_nsec_ll >> entry.file_size;

    // 读取剩余部分作为文件路径
    std::string path_str;
    // 跳过文件大小和路径之间的空格
    if (iss.peek() == ' ') {
        iss.ignore();
    }
    std::getline(iss, path_str); // 读取到行尾

    if (iss.fail() || path_str.empty()) { // 检查是否有读取错误或路径为空
        if (entry.mode.empty() && entry.blob_hash_hex.empty() && path_str.empty() && line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
            // 可能是空行，不是错误
            return std::nullopt; // 或者返回一个特殊标记表示空行
        }
        std::cerr << "错误: 解析索引行失败: " << line << std::endl;
        return std::nullopt;
    }

    if (entry.blob_hash_hex.length() != 40) return std::nullopt; // 哈希长度校验

    entry.file_path = std::filesystem::path(path_str).lexically_normal(); // 规范化路径

    // 将秒和纳秒部分组合回 time_point
    // 1. 将秒数和纳秒余数都转换为纳秒 (一个通用的高精度单位)
    std::chrono::nanoseconds total_nanoseconds_since_epoch =
        std::chrono::seconds(mtime_sec_ll) + std::chrono::nanoseconds(mtime_nsec_ll);

    // 2. 将这个总的纳秒 duration 显式地转换为 system_clock 的 duration 类型
    std::chrono::system_clock::duration duration_for_time_point =
        std::chrono::duration_cast<std::chrono::system_clock::duration>(total_nanoseconds_since_epoch);

    // 3. 使用这个 system_clock::duration 来构造 time_point
    entry.mtime = std::chrono::system_clock::time_point(duration_for_time_point);

    return entry;
}



// --- Index 实现 ---

Index::Index(const std::filesystem::path& biogit_dir_path)
    : index_file_path_(biogit_dir_path / Repository::INDEX_FILE_NAME) {

}

void Index::sort_entries_() {
    std::sort(entries_.begin(), entries_.end());
}

bool Index::load() {
    entries_.clear();
    loaded_ = false;

    if (!std::filesystem::exists(index_file_path_)) {
        loaded_ = true;
        return true;
    }

    std::ifstream ifs(index_file_path_);
    if (!ifs.is_open()) {
        std::cerr << "错误: 无法打开索引文件进行读取: " << index_file_path_.string() << std::endl;
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(ifs, line)) {
        line_number++;
        // 首先判断是否为纯粹的空行或者只包含空白字符的行
        if (line.find_first_not_of(" \t\n\v\f\r") == std::string::npos) {
            continue; // 是，则跳过
        }

        // 对于非空白行，尝试解析
        auto entry_opt = IndexEntry::parse_from_line(line);
        if (entry_opt) {
            entries_.push_back(*entry_opt);
        } else {
            // 如果 IndexEntry::parse_from_line 对一个非空白行返回了 std::nullopt，
            // 这意味着该行数据格式有问题。
            std::cerr << "错误 (Index::load): 解析索引文件第 " << line_number << " 行失败，索引可能已损坏: '" << line << "'" << std::endl;
            loaded_ = false;    // 标记加载失败
            entries_.clear();   // 清空已部分加载的条目，保持状态一致性
            // ifs.close(); // RAII 会处理关闭，但显式关闭也可以
            return false;       // 遇到无法解析的有效数据行，则认为加载失败
        }
    }

    // 检查循环是否因为流本身的错误（而不是正常EOF）而终止
    if (ifs.bad()) {
        std::cerr << "错误: 读取索引文件时发生底层I/O错误: " << index_file_path_.string() << std::endl;
        loaded_ = false;
        entries_.clear();
        return false;
    }
    // 如果循环正常结束（因为到达EOF），并且没有因为解析错误提前返回，那么加载是成功的。
    // ifs.fail() 在到达EOF时也会为true (eofbit被设置)，所以主要关注badbit。

    sort_entries_();
    loaded_ = true;
    return true;
}

bool Index::write() const {
    std::ofstream ofs(index_file_path_, std::ios::trunc); // trunc 会清空已存在的文件
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开索引文件进行写入: " << index_file_path_.string() << std::endl;
        return false;
    }

    for (const auto& entry : entries_) {
        ofs << entry.format_for_file() << "\n";
    }

    if (!ofs.good()) {
        std::cerr << "错误: 写入索引文件失败: " << index_file_path_.string() << std::endl;
        return false;
    }
    return true;
}


bool Index::add_or_update_entry(const std::filesystem::path& relative_path,
                                    const std::string& blob_hash_hex,
                                    const std::string& file_mode,
                                    const std::chrono::system_clock::time_point& mtime,
                                    uint64_t file_size) {
    if (!loaded_) {
        if (!load() && std::filesystem::exists(index_file_path_)) {
            // std::cerr << "错误: 添加条目前加载索引失败。" << std::endl;
            return false;
        }
    }

    // 假设 relative_path 已经是规范化的、相对于工作树根的路径
    IndexEntry new_entry;
    new_entry.mode = file_mode;
    new_entry.blob_hash_hex = blob_hash_hex;
    new_entry.file_path = relative_path;
    new_entry.mtime = mtime;
    new_entry.file_size = file_size;

    if (new_entry.blob_hash_hex.length() != 40) { // 基本验证
        std::cerr << "错误: 尝试添加的 IndexEntry 哈希无效: " << new_entry.file_path.string() << std::endl;
        return false;
    }

    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&relative_path](const IndexEntry& e){ return e.file_path == relative_path; });

    if (it != entries_.end()) {
        *it = new_entry; // 更新
    } else {
        entries_.push_back(new_entry); // 添加
    }

    sort_entries_(); // 添加或更新后排序
    return true;
}


bool Index::remove_entry(const std::filesystem::path& relative_path) {
    if (!loaded_) {
        if (!load() && std::filesystem::exists(index_file_path_)) {
            std::cerr << "警告: 尝试从未加载的索引中获取条目。" << std::endl;
            return false;
        }
    }

    // relative_path 已经是规范化的
    auto initial_size = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&relative_path](const IndexEntry& e){ return e.file_path == relative_path; }),
                   entries_.end());

    return entries_.size() < initial_size;
}

std::optional<IndexEntry> Index::get_entry(const std::filesystem::path& relative_file_path) const {
    std::filesystem::path normalized_path = relative_file_path.lexically_normal();

    auto it = std::lower_bound(entries_.begin(), entries_.end(), normalized_path,
        [](const IndexEntry& entry, const std::filesystem::path& path_val) {
            return entry.file_path < path_val;
        });

    if (it != entries_.end() && it->file_path == normalized_path) {
        return *it;
    }
    return std::nullopt;
}

const std::vector<IndexEntry>& Index::get_all_entries() const {
    return entries_;
}

void Index::clear_in_memory() {
    entries_.clear();
}


}
