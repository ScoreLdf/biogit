#include "../include/Repository.h"
#include "../include/RemoteClient.h"
#include "../include/object.h"
#include "../include/utils.h"

#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace Biogit {

using std::cout,std::endl;

// --- 定义静态常量成员 ---
const std::string Repository::MYGIT_DIR_NAME = ".biogit";
const std::string Repository::OBJECTS_DIR_NAME = "objects";
const std::string Repository::REFS_DIR_NAME = "refs";
const std::string Repository::HEADS_DIR_NAME = "heads";
const std::string Repository::TAGS_DIR_NAME = "tags";

const std::string Repository::HEAD_FILE_NAME = "HEAD";
const std::string Repository::INDEX_FILE_NAME = "index";
const std::string Repository::CONFIG_FILE_NAME = "config";
const std::string Repository::MERGE_HEAD_FILE_NAME="MERGE_HEAD";
const std::string Repository::FILE_CONFLICTS="FILE_CONFLICTS";


/**
 * @brief Repository 类的构造函数。
 * @details 初始化仓库对象的核心路径信息和索引管理器。
 * 此构造函数设计为私有或受保护，强制通过静态工厂方法 init() 或 load() 创建实例。
 * @param work_tree_path 仓库工作树的根目录的绝对路径。
 */
Repository::Repository(const std::filesystem::path& work_tree_path)
    : work_tree_root_(std::filesystem::absolute(work_tree_path)),
      mygit_dir_(work_tree_root_ / MYGIT_DIR_NAME) ,index_manager_(mygit_dir_){
}


/**
 * @brief 在指定的工作树路径下初始化一个新的 .biogit 仓库。
 * @param work_tree_path 要初始化仓库的工作树目录路径。默认为当前工作目录。
 * @return 如果成功，返回一个包含 Repository 对象的 std::optional；否则返回 std::nullopt。
 */
std::optional<Repository> Repository::init(const std::filesystem::path& work_tree_path) {
    std::error_code ec;

    // 1. 转换绝对路径，创建工作目录
    std::filesystem::path abs_work_tree_path = std::filesystem::absolute(work_tree_path);
    abs_work_tree_path = abs_work_tree_path.lexically_normal();
    if (!std::filesystem::exists(abs_work_tree_path, ec)) {
        if (!std::filesystem::create_directories(abs_work_tree_path, ec)) {
            std::cerr << "错误: 无法创建工作目录 '" << abs_work_tree_path.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
        std::cout << "创建工作目录: " << abs_work_tree_path.string() << std::endl;
    }

    // 2. 检查 .biogit 目录是否已存在
    std::filesystem::path mygit_path = abs_work_tree_path / MYGIT_DIR_NAME;
    if (std::filesystem::exists(mygit_path, ec)) {
        std::cerr << "提示: '" << mygit_path.string() << "' 已存在。" << std::endl;
        return std::nullopt;
    } else {
        // 3. 创建 .biogit 目录
        if (!std::filesystem::create_directory(mygit_path, ec)) {
            std::cerr << "错误: 无法创建 .biogit 目录 '" << mygit_path.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
    }

    // 4. 创建内部子目录
    std::vector<std::filesystem::path> sub_dirs_to_create = {
        mygit_path / OBJECTS_DIR_NAME,
        mygit_path / REFS_DIR_NAME,
        mygit_path / REFS_DIR_NAME / HEADS_DIR_NAME,
        mygit_path / REFS_DIR_NAME / TAGS_DIR_NAME
    };

    for (const auto& dir : sub_dirs_to_create) {
        if (!std::filesystem::exists(dir, ec)) {
            if (!std::filesystem::create_directory(dir, ec)) {
                std::cerr << "错误: 无法创建内部目录 '" << dir.string() << "': " << ec.message() << std::endl;
                // TODO  理论上，这里应该尝试清理已创建的 .biogit 目录及其他子目录，以保持原子性
                return std::nullopt;
            }
        }
    }

    // 5. 创建初始文件
    // HEAD 文件 - 指向默认分支 (例如 main)
    std::filesystem::path head_file = mygit_path / HEAD_FILE_NAME;
    std::ofstream head_ofs(head_file);
    if (!head_ofs.is_open()) {
        std::cerr << "错误: 无法创建 HEAD 文件。" << std::endl;
        return std::nullopt;
    }
    head_ofs << "ref: refs/heads/main\n";
    head_ofs.close();

    // index 文件  - 初始为空
    std::filesystem::path index_file = mygit_path / INDEX_FILE_NAME;
    std::ofstream index_ofs(index_file); // 创建空文件
    if (!index_ofs.is_open()) {
        std::cerr << "错误: 无法创建 stage 文件。" << std::endl;
        return std::nullopt;
    }
    index_ofs.close();

    std::cout << "已初始化空的 BioGit 仓库于 " << mygit_path.string() << std::endl;
    return Repository(abs_work_tree_path); // 使用私有构造函数创建并返回对象
}

/**
 * @brief 加载一个已存在的 .biogit 仓库。
 * @param work_tree_path 已确定的仓库工作树根目录路径。
 * @return 如果成功加载，返回 Repository 对象；否则返回 std::nullopt。
 */
std::optional<Repository> Repository::load(const std::filesystem::path &work_tree_path) {
    std::error_code ec;
    // 1. 获取并规范化工作树的绝对路径
    std::filesystem::path abs_work_tree_path = std::filesystem::absolute(work_tree_path);
    abs_work_tree_path = abs_work_tree_path.lexically_normal();

    // 2. 构造 .biogit 目录的路径
    std::filesystem::path mygit_path = abs_work_tree_path / MYGIT_DIR_NAME;

    // 3. 检查核心目录和文件是否存在，以判断是否为有效的 BioGit 仓库
    if (std::filesystem::exists(mygit_path, ec) &&
        std::filesystem::is_directory(mygit_path, ec) &&
        std::filesystem::exists(mygit_path / HEAD_FILE_NAME, ec) &&
        std::filesystem::exists(mygit_path / OBJECTS_DIR_NAME, ec) &&
        std::filesystem::exists(mygit_path / REFS_DIR_NAME, ec)) {
        return Repository(abs_work_tree_path);
    }
    std::cerr << "错误: '" << abs_work_tree_path.string() << "' 不是一个有效的 BioGit 仓库工作树根目录。" << std::endl;
    return std::nullopt;
}


/**
 * @brief 将指定的文件或目录添加到索引 (暂存区)。
 * @param path_to_add_original 要添加的文件或目录的路径 (可以是绝对路径或相对于当前工作目录的路径)。
 * @return 如果操作成功，返回 true；否则返回 false。
 */
bool Repository::add(const std::filesystem::path &path_to_add_original) {
    // 1. 确保索引已从磁盘加载到 index_manager_
    if (!index_manager_.is_loaded()) {
        if (!index_manager_.load() && std::filesystem::exists(mygit_dir_ / INDEX_FILE_NAME)) {
            std::cerr << "错误: add 操作前，索引文件 '"
                      << (mygit_dir_ / INDEX_FILE_NAME).string()
                      << "' 存在但加载失败。" << std::endl;
            return false;
        }
    }

    std::error_code ec; // 用于捕获文件系统操作的错误

    // --- 步骤 A: 确定要处理的文件列表 ---
    std::vector<std::filesystem::path> files_to_process;

    // A.1 将用户输入的路径转换为绝对路径并规范化
    std::filesystem::path abs_path_to_add;
    if (path_to_add_original.is_absolute()) {
        abs_path_to_add = path_to_add_original;
    } else {
        abs_path_to_add = std::filesystem::current_path(ec) / path_to_add_original;
        if (ec) {
            std::cerr << "错误: 无法获取当前工作目录来解析相对路径 '"
                      << path_to_add_original.string() << "': " << ec.message() << std::endl;
            return false;
        }
    }

    // A.2 检查规范化后的路径是否存在于文件系统
    abs_path_to_add = abs_path_to_add.lexically_normal();
    if (!std::filesystem::exists(abs_path_to_add, ec)) {
        std::cerr << "错误: 指定的路径不存在: " << abs_path_to_add.string() << std::endl;
        return false;
    }

    // A.3 根据路径是目录还是文件，填充 files_to_process 列表
    if (std::filesystem::is_directory(abs_path_to_add, ec)) {  // 如果是目录，则深度优先地遍历所有文件和子目录
        std::filesystem::recursive_directory_iterator dir_iter(abs_path_to_add,
                                                               std::filesystem::directory_options::skip_permission_denied, // 跳过无权限目录/文件
                                                               ec);
        if (ec) {
            std::cerr << "错误: 无法遍历目录 '" << abs_path_to_add.string() << "': " << ec.message() << std::endl;
            return false;
        }
        for (const auto& dir_entry : dir_iter) {
            std::error_code entry_ec;
            if (dir_entry.is_regular_file(entry_ec)) { // 只处理常规文件，忽略目录本身、符号链接等
                // 跳过 .biogit 目录内的所有内容
                if (dir_entry.path().string().rfind(mygit_dir_.string(), 0) == 0) {
                    continue;
                }
                files_to_process.push_back(dir_entry.path().lexically_normal());
            } else if (entry_ec) {
                std::cerr << "警告: 检查路径 '" << dir_entry.path().string()
                          << "' 类型时出错: " << entry_ec.message() << std::endl;
            }
        }
    } else if (std::filesystem::is_regular_file(abs_path_to_add, ec)) { // 如果是常规文件，将其添加到待处理列表
        // 跳过 .biogit 目录内的所有内容
        if (abs_path_to_add.string().rfind(mygit_dir_.string(), 0) == 0) {
            std::cout << "提示 (add): 不能添加 .biogit 目录内部的文件: '" << abs_path_to_add.string() << "'" << std::endl;
        } else {
            files_to_process.push_back(abs_path_to_add);
        }
    } else {   // 如果既不是目录也不是常规文件（例如，符号链接、设备文件，或者路径无效）
        std::cerr << "错误: 指定路径 '" << abs_path_to_add.string()
                  << "' 既不是常规文件也不是目录，无法添加。" << std::endl;
        return false;
    }

    if (ec) {
        std::cerr << "错误: 检查路径 '" << abs_path_to_add.string()
                  << "' 类型时出错: " << ec.message() << std::endl;
        return false;
    }

    if (files_to_process.empty()) {
        std::cout << "提示: 没有找到要添加的文件于路径 '" << path_to_add_original.string() << "'。" << std::endl;
        return true;
    }

    // --- 步骤 B: 循环处理每个找到的文件 ---
    bool overall_success = true; // 跟踪整个 add 操作是否所有文件都成功

    for (const auto& current_file_abs_path : files_to_process) {
        // B.1 将文件的绝对路径转换为相对于工作树根目录的路径
        std::filesystem::path relative_path;
        relative_path = std::filesystem::relative(current_file_abs_path, work_tree_root_, ec);

        if (ec) {
            std::cerr << "错误: 计算文件 '" << current_file_abs_path.string()
                      << "' 的相对路径失败: " << ec.message() << std::endl;
            overall_success = false;
            continue; // 处理下一个文件
        }
        if (relative_path.empty() || relative_path.string().rfind("..", 0) == 0) {
            std::cerr << "错误: 文件 '" << current_file_abs_path.string()
                      << "' 不在工作树 '" << work_tree_root_.string() << "' 内部。" << std::endl;
            overall_success = false;
            continue;
        }
        relative_path = relative_path.lexically_normal();

        // B.2. 读取文件内容
        std::ifstream file_stream(current_file_abs_path, std::ios::binary);
        if (!file_stream.is_open()) {
            std::cerr << "错误: 无法打开文件 '" << current_file_abs_path.string() << "' 进行读取。" << std::endl;
            overall_success = false;
            continue;
        }

        file_stream.seekg(0, std::ios::end);
        std::streamsize file_size_on_disk = file_stream.tellg();
        file_stream.seekg(0, std::ios::beg);
        std::vector<std::byte> file_content_bytes(static_cast<size_t>(file_size_on_disk));
        if (file_size_on_disk > 0) {
            if (!file_stream.read(reinterpret_cast<char*>(file_content_bytes.data()), file_size_on_disk)) {
                std::cerr << "错误: 读取文件内容失败: " << current_file_abs_path.string() << std::endl;
                overall_success = false;
                file_stream.close();
                continue;
            }
        }
        file_stream.close();

        // B.3. 创建 Blob 对象并保存到对象库
        Blob blob_to_save(file_content_bytes);
        std::optional<std::string> blob_hash_opt = blob_to_save.save(get_objects_directory());

        if (!blob_hash_opt) {
            std::cerr << "错误: 保存文件 '" << current_file_abs_path.string() << "' 的 Blob 对象失败。" << std::endl;
            overall_success = false;
            continue;
        }
        std::string blob_hash = *blob_hash_opt;

        // B.4. 获取文件模式和元数据
        std::string file_mode_str = "100644"; // TODO 简化成普通文件模式
        auto last_write_time = std::filesystem::last_write_time(current_file_abs_path, ec);
        if (ec) {
            std::cerr << "错误: 无法获取文件 '" << current_file_abs_path.string() << "' 的最后修改时间: " << ec.message() << std::endl;
            overall_success = false;
            continue;
        }

        auto file_time_duration_since_epoch = last_write_time.time_since_epoch();
        auto system_clock_compatible_duration =
            std::chrono::duration_cast<std::chrono::system_clock::duration>(file_time_duration_since_epoch);
        std::chrono::system_clock::time_point mtime(system_clock_compatible_duration); // 直接构造 mtime

        uint64_t file_size_val = static_cast<uint64_t>(file_size_on_disk); // 已在读取时获取

        // B.5. 将文件信息更新到索引管理器 (内存中)
        if (!index_manager_.add_or_update_entry(relative_path, blob_hash, file_mode_str, mtime, file_size_val)) {
            std::cerr << "错误: 更新文件 '" << relative_path.string() << "' 到索引失败。" << std::endl;
            overall_success = false;
        } else {
            std::cout << "已暂存: " << relative_path.string() << std::endl;
        }
    } // 文件处理循环结束

    // --- 步骤 C: 将更新后的索引写回磁盘 ---
    if (overall_success) { // 只有在所有文件处理（尝试）都未导致致命错误时才考虑写入
        if (!index_manager_.write()) {
            std::cerr << "严重错误: 写入索引文件失败！暂存的更改可能未保存。" << std::endl;
            return false; // 索引写入失败是严重错误
        }
    } else {
        std::cout << "由于处理过程中发生错误，部分或全部文件可能未成功添加到索引。" << std::endl;
        return false; // 返回整体失败
    }

    return overall_success; // 如果所有文件处理都成功，并且索引写入也成功
}


/**
 * @brief 从索引中移除文件，但保留工作目录中的文件 (类似 git rm --cached)。
 * @param path_to_remove 要从索引中移除的文件的路径 (可以是绝对路径或相对于当前工作目录的路径)。
 * @return 如果操作成功，返回 true；否则返回 false。
 */
bool Repository::rm_cached(const std::filesystem::path& path_to_remove_original) {
    // 1. 确保索引已从磁盘加载到 index_manager_
    if (!index_manager_.is_loaded()) {
        if (!index_manager_.load() && std::filesystem::exists(mygit_dir_ / INDEX_FILE_NAME)) {
            std::cerr << "错误: rm_cached 操作前，索引文件 '"
                      << (mygit_dir_ / INDEX_FILE_NAME).string()
                      << "' 存在但加载失败。" << std::endl;
            return false;
        }
    }

    std::error_code ec;

    // --- 步骤 A: 将输入路径转换为规范化的、相对于工作树根目录的路径 ---
    std::filesystem::path absolute_path_to_remove;
    if (path_to_remove_original.is_absolute()) {
        absolute_path_to_remove = path_to_remove_original;
    } else {
        absolute_path_to_remove = std::filesystem::current_path(ec) / path_to_remove_original;
        if (ec) {
            std::cerr << "错误: 无法获取当前工作目录来解析相对路径 '"
                      << path_to_remove_original.string() << "': " << ec.message() << std::endl;
            return false;
        }
    }
    absolute_path_to_remove = absolute_path_to_remove.lexically_normal();


    std::filesystem::path relative_path_for_index = std::filesystem::relative(absolute_path_to_remove, work_tree_root_, ec);

    if (ec) {
        std::cerr << "错误: 为 '" << path_to_remove_original.string() << "' 计算相对路径失败: " << ec.message() << std::endl;
        return false;
    }
    // 验证相对路径是否在工作树内
    if (relative_path_for_index.empty() || relative_path_for_index.string().rfind("..", 0) == 0) {
        // 如果 relative_path_for_index 为空，但 absolute_path_to_remove == work_tree_root_，
        // 相对路径可能是 "."。但 rm 通常针对的是文件。
        // Git rm --cached . 会报错 "fatal: pathspec '.' did not match any files" 如果没有文件匹配
        // 这里简化，如果路径不在工作树内或者为空（非 "."）则报错。
        if (absolute_path_to_remove == work_tree_root_ && relative_path_for_index.empty()){
             relative_path_for_index = ".";
             std::cerr << "警告: 尝试从索引中移除工作树根目录本身。通常这是无效操作。" << std::endl;
             return false;
        } else if (relative_path_for_index.empty()) {
             std::cerr << "错误: 文件 '" << path_to_remove_original.string()
                      << "' (解析为 '"<< absolute_path_to_remove.string()
                      << "') 不在工作树 '" << work_tree_root_.string() << "' 内 (无法计算有效相对路径)。" << std::endl;
            return false;
        }
        if (relative_path_for_index.string().rfind("..", 0) == 0){
             std::cerr << "错误: 文件 '" << path_to_remove_original.string()
                      << "' (解析为 '"<< absolute_path_to_remove.string()
                      << "') 位于工作树 '" << work_tree_root_.string() << "' 外部。" << std::endl;
            return false;
        }
    }
    relative_path_for_index = relative_path_for_index.lexically_normal();

    // --- 步骤 B: 从索引管理器中移除条目 ---
    bool entry_was_actually_removed = index_manager_.remove_entry(relative_path_for_index);

    if (entry_was_actually_removed) {
        // --- 步骤 C: 如果确实有条目被移除，则写回索引文件 ---
        if (!index_manager_.write()) {
            std::cerr << "严重错误: 从索引移除 '" << relative_path_for_index.string()
                      << "' 后，写入索引文件失败！更改可能未保存。" << std::endl;
            return false; // 索引写入失败是严重错误
        }
        std::cout << "已从索引移除: " << relative_path_for_index.string() << std::endl;
    } else {
        std::cout << "提示: 文件 '" << relative_path_for_index.string() << "' 原本就不在索引中。" << std::endl;
    }

    return true;
}


/**
 * @brief 从工作目录和索引中移除文件 (类似 git rm)。
 * @param path_to_remove 要移除的文件的路径 (可以是绝对路径或相对于当前工作目录的路径)。
 * @return 如果操作成功，返回 true；否则返回 false。
 */
bool Repository::rm(const std::filesystem::path& path_to_remove_original) {
    // 1. 确保索引已从磁盘加载到 index_manager_
    if (!index_manager_.is_loaded()) {
        if (!index_manager_.load() && std::filesystem::exists(mygit_dir_ / INDEX_FILE_NAME)) { //
            std::cerr << "错误: rm 操作前，索引文件 '"
                      << (mygit_dir_ / INDEX_FILE_NAME).string()
                      << "' 存在但加载失败。" << std::endl;
            return false;
        }
    }

    std::error_code ec;

    // --- 步骤 A: 将输入路径转换为规范化的、相对于工作树根目录的路径 ---
    std::filesystem::path absolute_path_to_remove_in_wd;
    if (path_to_remove_original.is_absolute()) {
        absolute_path_to_remove_in_wd = path_to_remove_original;
    } else {
        absolute_path_to_remove_in_wd = std::filesystem::current_path(ec) / path_to_remove_original;
        if (ec) {
            std::cerr << "错误: 无法获取当前工作目录来解析相对路径 '"
                      << path_to_remove_original.string() << "': " << ec.message() << std::endl;
            return false;
        }
    }
    absolute_path_to_remove_in_wd = absolute_path_to_remove_in_wd.lexically_normal();

    std::filesystem::path relative_path_for_index = std::filesystem::relative(absolute_path_to_remove_in_wd, work_tree_root_, ec); //

    if (ec) {
        std::cerr << "错误: 为 '" << path_to_remove_original.string() << "' 计算相对路径失败: " << ec.message() << std::endl;
        return false;
    }
    // 路径有效性检查
    if (relative_path_for_index.empty() || relative_path_for_index.string().rfind("..", 0) == 0) {
        if (absolute_path_to_remove_in_wd == work_tree_root_ && relative_path_for_index.empty()){
             std::cerr << "错误: 不支持移除工作树根目录 '.'" << std::endl;
             return false;
        } else if (relative_path_for_index.empty()) {
             std::cerr << "错误: 文件 '" << path_to_remove_original.string()
                      << "' (解析为 '"<< absolute_path_to_remove_in_wd.string()
                      << "') 不在工作树 '" << work_tree_root_.string() << "' 内 (无法计算有效相对路径)。" << std::endl;
            return false;
        }
        if (relative_path_for_index.string().rfind("..", 0) == 0){ //
             std::cerr << "错误: 文件 '" << path_to_remove_original.string()
                      << "' (解析为 '"<< absolute_path_to_remove_in_wd.string()
                      << "') 位于工作树 '" << work_tree_root_.string() << "' 外部。" << std::endl;
            return false;
        }
    }
    relative_path_for_index = relative_path_for_index.lexically_normal();

    // --- 步骤 B: 检查文件是否在索引中 ---
    std::optional<IndexEntry> entry_opt = index_manager_.get_entry(relative_path_for_index); //
    bool was_in_index = entry_opt.has_value();

    if (!was_in_index) {
        std::cerr << "错误: '" << relative_path_for_index.string()
                  << "' 与 biogit 已知的任何文件不匹配" << std::endl;
        return false;
    }
    const IndexEntry& entry_in_index_ref = *entry_opt;

    // --- 步骤 C: 严格检查 - 工作目录中的文件是否与索引中的版本不同 ---
    bool existed_in_wd = std::filesystem::exists(absolute_path_to_remove_in_wd);

    if (existed_in_wd) {
        // 1. 读取工作目录文件内容
        std::ifstream file_stream_wd(absolute_path_to_remove_in_wd, std::ios::binary);
        if (!file_stream_wd.is_open()) {
            std::cerr << "错误: 无法打开工作目录文件 '" << absolute_path_to_remove_in_wd.string() << "' 以进行严格检查。" << std::endl;
            return false;
        }
        file_stream_wd.seekg(0, std::ios::end);
        std::streamsize file_size_wd = file_stream_wd.tellg();
        file_stream_wd.seekg(0, std::ios::beg);
        std::vector<std::byte> wd_content_bytes(static_cast<size_t>(file_size_wd));
        if (file_size_wd > 0) {
            if (!file_stream_wd.read(reinterpret_cast<char*>(wd_content_bytes.data()), file_size_wd)) {
                std::cerr << "错误: 读取工作目录文件 '" << absolute_path_to_remove_in_wd.string() << "' 内容失败以进行严格检查。" << std::endl;
                file_stream_wd.close();
                return false;
            }
        }
        file_stream_wd.close();

        // 2. 创建工作目录内容的 Blob 对象并计算其序列化后的哈希
        Blob wd_blob(wd_content_bytes); //
        std::vector<std::byte> serialized_wd_blob_data = wd_blob.serialize(); //
        std::string wd_blob_hash = SHA1::sha1(serialized_wd_blob_data); //

        // 3. 与索引中的 Blob 哈希进行比较
        if (wd_blob_hash != entry_in_index_ref.blob_hash_hex) { //
            std::cerr << "错误: '" << relative_path_for_index.string()
                      << "' 在工作目录中已被修改且其更改未暂存。" << std::endl;
            std::cerr << "请使用 'biogit add " << relative_path_for_index.string()
                      << "' 来暂存这些更改，或者使用 'biogit rm --cached " << relative_path_for_index.string()
                      << "' 来仅从索引中移除并保留工作目录中的文件。" << std::endl;
            return false;
        }
    }
    // 如果文件在索引中，但不在工作目录 'git rm' 通常会继续从索引中移除它，这不视为错误。因此严格检查主要针对工作目录中存在且有差异的情况。

    // --- 步骤 D: 从索引中移除 ---
    bool index_removal_succeeded = rm_cached(relative_path_for_index);

    if (!index_removal_succeeded) {
        // rm_cached 内部已打印错误信息
        return false;
    }

    // --- 步骤 E: 从工作目录中移除 (如果之前存在) ---
    if (existed_in_wd) { // 只有在工作目录中确实存在该文件时才尝试删除
        std::error_code ec_remove_wd;
        if (std::filesystem::remove(absolute_path_to_remove_in_wd, ec_remove_wd)) {
            std::cout << "rm '" << relative_path_for_index.string() << "'" << std::endl;
            return true; // 成功从索引和工作目录移除
        } else {
            std::cerr << "错误: 从工作目录移除 '" << absolute_path_to_remove_in_wd.string() << "' 失败: " << ec_remove_wd.message() << std::endl;
            std::cerr << "注意: 文件已从索引移除，但工作目录中的副本删除失败。" << std::endl;
            return false; // 从索引移除成功，但工作目录移除失败
        }
    } else {
        // 文件在索引中，但不在工作目录中。
        // rm_cached 已处理索引移除并打印了相关消息。操作视为成功。
        return true;
    }
}



/**
 * @brief 将索引 (暂存区) 中的更改提交到版本历史。
 * @param message 提交信息。
 * @return 新Commit的哈希值 (如果成功)；否则返回 std::nullopt。
 * @details
 * 1.读取index 判断是否为合并提交（.biogit/MERGE_HEAD）是这获取commit2
 * 2.获取当前 HEAD 的提交commit1和树哈希tree1
 * 3.当前index构建tree2  普通提交不允许tree1=tree2 直接返回 ，合并提交运行
 * 4.确认父提交 普通提交只有commit1（或者为空即根提交）  合并提交有两个
 * 5.刷新HEAD和index
 */
std::optional<std::string> Repository::commit(const std::string& message) {
    // 1. 加载索引 (确保 index_manager_ 是最新的)
    if (!index_manager_.is_loaded()) {
        if (!index_manager_.load() && std::filesystem::exists(mygit_dir_ / INDEX_FILE_NAME)) { //
            std::cerr << "错误: commit 操作前加载索引失败。" << std::endl;
            return std::nullopt;
        }
    }

    // 保证是有序的 因为构建 Tree需要有序
    std::vector<IndexEntry> index_entries = index_manager_.get_all_entries(); // 获取副本或const引用
    std::sort(index_entries.begin(), index_entries.end()); //


    // 检查 MERGE_HEAD 文件是否存在，以判断是否正在进行合并提交
    std::filesystem::path merge_head_file = mygit_dir_ / MERGE_HEAD_FILE_NAME;
    bool is_completing_merge = std::filesystem::exists(merge_head_file);


    // 2. 获取当前 HEAD 指向的 Commit 的哈希 (这将是 OURS 父节点，或者是普通提交的唯一父节点)
    std::optional<std::string> head_commit_hash_opt = _get_head_commit_hash(); //
    std::string head_tree_hash_for_comparison; // 用于与新构建的树比较，判断是否有实际更改
    if (head_commit_hash_opt) {
        auto head_commit_obj_opt = Commit::load_by_hash(*head_commit_hash_opt, get_objects_directory()); //
        if (head_commit_obj_opt) {
            head_tree_hash_for_comparison = head_commit_obj_opt->tree_hash_hex;
        }
        // 如果加载 HEAD Commit 失败，head_tree_hash_for_comparison 会保持为空
    }

    // 如果不是正在完成合并，并且索引为空且没有HEAD（初始状态），则无内容提交
    if (!is_completing_merge && index_entries.empty() && !head_commit_hash_opt) {
        std::cout << "没有要提交的内容 (索引为空，且无历史提交)。" << std::endl;
        return std::nullopt;
    }
    // 如果索引为空，但正在完成合并（意味着所有更改都已被解决并清空，或者合并结果是删除所有文件）
    // 这种情况是允许的，因为合并提交本身就是一个记录。

    // 3. 从索引条目构建 Tree 对象，并获取根 Tree 的哈希
    std::optional<std::string> root_tree_hash_opt = _build_trees_and_get_root_hash(index_entries); //
    if (!root_tree_hash_opt) {
        std::cerr << "错误: 构建 Tree 对象失败。" << std::endl;
        return std::nullopt;
    }
    std::string root_tree_hash = *root_tree_hash_opt;

    // 检查新构建的树是否与HEAD的树相同 (避免没有实际更改的提交)
    // 对于合并提交，即使树相同（例如，解决冲突后与HEAD一致，但仍需记录合并历史），也应该允许提交。
    if (!is_completing_merge && head_commit_hash_opt && root_tree_hash == head_tree_hash_for_comparison) { // 修改了判断条件
        std::cout << "没有要提交的更改 (索引与上次提交内容一致)。" << std::endl;
        return std::nullopt;
    }


    // 4. 获取父 Commit 哈希
    std::vector<std::string> parent_commit_hashes; // 用于存储父提交哈希的列表
    std::filesystem::path head_file = get_head_file_path(); // 获取 .biogit/HEAD 文件的路径
    std::string current_branch_ref_path_for_update; // 用于后续步骤8更新分支文件

    // 在分离 HEAD 时，current_branch_ref_path_for_update 为空
    if (std::filesystem::exists(head_file)) {
        std::ifstream head_ifs_for_parent(head_file); // 重命名以避免与后续的 head_ofs 冲突
        std::string head_content_line;
        if (std::getline(head_ifs_for_parent, head_content_line)) {
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) head_content_line.pop_back();
            if (!head_content_line.empty() && head_content_line.back() == '\r') head_content_line.pop_back();

            if (head_content_line.rfind("ref: ", 0) == 0 && head_content_line.length() > 5) {
                current_branch_ref_path_for_update = head_content_line.substr(5);
                current_branch_ref_path_for_update.erase(0, current_branch_ref_path_for_update.find_first_not_of(" "));
                // 第一个父提交应该是当前分支指向的 commit
                if (head_commit_hash_opt) { // head_commit_hash_opt 是当前 HEAD 解析出的 commit
                    parent_commit_hashes.push_back(*head_commit_hash_opt);
                }
            } else if (head_content_line.length() == 40) { // HEAD 分离
                 if (head_commit_hash_opt && head_content_line == *head_commit_hash_opt) {
                    parent_commit_hashes.push_back(*head_commit_hash_opt);
                 } else if (head_commit_hash_opt) { // head_content_line 可能与解析的不一致？理论上应该一致
                     std::cerr << "警告: HEAD文件内容与解析的HEAD commit不一致。" << std::endl;
                     parent_commit_hashes.push_back(*head_commit_hash_opt); // 优先使用 _get_head_commit_hash 的结果
                 } else { // 如果 _get_head_commit_hash 也失败了，但HEAD文件有内容
                     parent_commit_hashes.push_back(head_content_line);
                 }
                // current_branch_ref_path_for_update 会保持为空
            }
        }
        head_ifs_for_parent.close();
    }
    // 如果是初始提交 (head_commit_hash_opt 为空)，parent_commit_hashes 保持为空。

    // 如果是正在完成合并，添加第二个父提交
    if (is_completing_merge) {
        if (parent_commit_hashes.empty() && head_commit_hash_opt) {
            // 这种情况发生在：HEAD是分离的，且是第一个父节点，或者HEAD文件损坏但_get_head_commit_hash仍能工作
            // 对于合并，OURS 必须存在
             parent_commit_hashes.push_back(*head_commit_hash_opt);
        }
        if (parent_commit_hashes.empty()) { // 仍然为空，意味着无法确定OURS父节点
            std::cerr << "错误: 正在完成合并，但无法确定第一个父提交 (OURS)。" << std::endl;
            return std::nullopt;
        }

        std::ifstream m_head_ifs(merge_head_file);
        std::string theirs_commit_hash;
        if (m_head_ifs >> theirs_commit_hash && theirs_commit_hash.length() == 40) {
            parent_commit_hashes.push_back(theirs_commit_hash); // THEIRS 作为第二个父节点
        } else {
            std::cerr << "错误: 无法从 MERGE_HEAD 读取有效的第二个父 commit 哈希。" << std::endl;
            m_head_ifs.close();
            return std::nullopt;
        }
        m_head_ifs.close();
    }


    // 5. 获取作者和提交者信息
    PersonTimestamp author_info; //
    PersonTimestamp committer_info; //
    std::string user_name = "Default User";
    std::string user_email = "user@example.com";
    auto user_cfg_opt = get_user_config();
    if (user_cfg_opt) {
        if (!user_cfg_opt->name.empty()) user_name = user_cfg_opt->name;
        if (!user_cfg_opt->email.empty()) user_email = user_cfg_opt->email;
    } else {
        std::cout << "提示: 未配置用户信息 (user.name 和 user.email)。正在使用默认值。" << std::endl;
        std::cout << "提示: 可以使用 'biogit2 config user.name \"Your Name\"' 和 'biogit2 config user.email \"you@example.com\"' 来设置。" << std::endl;
    }
    auto current_time_point = std::chrono::system_clock::now();
    std::time_t current_time_t = std::chrono::system_clock::to_time_t(current_time_point);
    std::tm local_tm = *std::localtime(&current_time_t);
    char tz_buffer[6];
    if (std::strftime(tz_buffer, sizeof(tz_buffer), "%z", &local_tm)) {
        author_info.timezone_offset = tz_buffer;
    } else { author_info.timezone_offset = "+0000"; }
    author_info.name = user_name;
    author_info.email = user_email;
    author_info.timestamp = current_time_point;
    committer_info = author_info;


    // 6. 创建 Commit 对象
    Commit new_commit_obj;
    new_commit_obj.tree_hash_hex = root_tree_hash;
    new_commit_obj.parent_hashes_hex = parent_commit_hashes; // 可能有0, 1, 或 2 个父哈希
    new_commit_obj.author = author_info;
    new_commit_obj.committer = committer_info;
    new_commit_obj.message = message;

    // 7. 保存 Commit 对象
    auto new_commit_hash_opt = new_commit_obj.save(get_objects_directory()); //
    if (!new_commit_hash_opt) {
        std::cerr << "错误: 保存新的 Commit 对象失败。" << std::endl;
        return std::nullopt;
    }
    std::string new_commit_hash = *new_commit_hash_opt;
    std::cout << "已提交 ["
              << (parent_commit_hashes.empty() ? "根提交" : (is_completing_merge ? "合并提交" : "新提交")) // 根据情况显示提交类型
              << " " << new_commit_hash.substr(0, 7) << "] "
              << message.substr(0, message.find('\n')) << std::endl;


    // 8. 更新分支引用 (或 HEAD 如果是分离头)
    if (!current_branch_ref_path_for_update.empty()) {
        // 如果 HEAD 指向一个分支 (例如 "refs/heads/main")，则更新该分支文件
        std::filesystem::path branch_file_to_update = mygit_dir_ / current_branch_ref_path_for_update;
        std::ofstream branch_ofs(branch_file_to_update, std::ios::trunc);
        if (!branch_ofs.is_open()) {
            std::cerr << "严重错误: 无法打开分支文件 '" << branch_file_to_update.string() << "' 进行更新！" << std::endl;
            // 即使引用更新失败，commit 对象已创建，返回其哈希
            return new_commit_hash;
        }
        branch_ofs << new_commit_hash << std::endl;
        branch_ofs.close();
    } else { // current_branch_ref_path_for_update 为空，意味着是分离头或初始提交
        // 分离头指针状态 或 仓库的第一次提交且HEAD文件初始内容不规范
        // 直接更新 .biogit/HEAD 文件
        std::ofstream head_ofs(head_file, std::ios::trunc);
        if (!head_ofs.is_open()) {
            std::cerr << "严重错误: 无法打开 HEAD 文件进行更新！" << std::endl;
            return new_commit_hash;
        }
        head_ofs << new_commit_hash << std::endl;
        head_ofs.close();
        if (parent_commit_hashes.empty()) { // 针对根提交的提示
             std::cout << "提示: 仓库的第一次提交，HEAD 现在直接指向此提交 (分离状态)。" << std::endl;
        } else if (!is_completing_merge) { // 对于非合并提交的分离HEAD提示
             std::cout << "提示: HEAD 处于分离状态，已更新指向新的提交。" << std::endl;
        }
        // 如果是合并提交完成且HEAD原先是分离的，此时HEAD会指向新的合并commit，仍然是分离的。
        // git merge 在分离头时，也会使 HEAD 指向新的合并 commit。
    }

    // 如果是合并提交成功，删除 MERGE_HEAD 文件
    if (is_completing_merge) {
        std::error_code ec_remove_merge_head;
        if (std::filesystem::exists(merge_head_file)) { // 再次检查以防万一
            if (!std::filesystem::remove(merge_head_file, ec_remove_merge_head)) {
                std::cerr << "警告: 无法删除 MERGE_HEAD 文件: " << ec_remove_merge_head.message() << std::endl;
            } else {
                if (std::filesystem::exists(mygit_dir_ / FILE_CONFLICTS)) {
                    std::filesystem::remove(mygit_dir_ / FILE_CONFLICTS);
                }
                std::cout << "MERGE_HEAD 文件已删除，合并完成。" << std::endl;
            }
        }

    }

    // --- 步骤 9: 更新索引以匹配新的 HEAD ---
    // 无论是否合并提交，成功提交后，索引都应该与新的 commit 的树一致
    index_manager_.clear_in_memory(); // 先清空内存中的旧条目
    _populate_index_from_tree_recursive(root_tree_hash, "", index_manager_); // 用新 commit 的 tree 填充索引

    if (!index_manager_.write()) { // 将更新后的索引写回磁盘
        std::cerr << "警告: 提交后更新索引文件失败。" << std::endl;
        // 即使索引写入失败，commit 也已创建，所以不在这里 return std::nullopt
    }

    return new_commit_hash;
}



/**
 * @brief 显示当前仓库的状态。
 * 它会比较 HEAD Commit、索引 (暂存区) 和工作目录之间的差异，
 * 并列出已暂存的更改、未暂存的更改以及未被追踪的文件。
 * HEAD 指向分支
 *      分支refs/heads/ 是否存在 -> 读取commit
 *                      不存在  -> 没有commit
 * HEAD 分离  ->  读取commit
 *
 * HEAD 状态未知/为空/没有HEAD    -> 没有commit
*/
void Repository::status() const {
    std::error_code ec;

    // --- 1. 获取并打印当前分支信息 和 初始化状态判断 ---
    std::string current_branch_display_name = "HEAD (分离状态)";
    bool is_on_branch = false;
    bool is_repository_empty = true; // 先假设仓库是空的 (没有提交)

    std::filesystem::path head_file_path_val = get_head_file_path();
    std::optional<std::string> head_commit_hash_opt = _get_head_commit_hash();

    if (std::filesystem::exists(head_file_path_val)) {
        std::ifstream head_ifs(head_file_path_val);
        std::string head_content_line;
        if (std::getline(head_ifs, head_content_line)) {
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) {
                head_content_line.pop_back();
            }
            if (!head_content_line.empty() && head_content_line.back() == '\r') {
                head_content_line.pop_back();
            }

            if (head_content_line.rfind("ref: refs/heads/", 0) == 0) {
                is_on_branch = true;
                current_branch_display_name = head_content_line.substr(std::string("ref: refs/heads/").length());
                if (head_commit_hash_opt) {
                    is_repository_empty = false;
                } else {
                    // std::cout << "当前分支 '" << current_branch_display_name << "' 尚无提交。" << std::endl; // 这条信息可能在后面处理
                }
            } else if (head_content_line.length() == 40) {
                current_branch_display_name = "HEAD (分离于 " + head_content_line.substr(0, 7) + ")";
                if (head_commit_hash_opt) {
                    is_repository_empty = false;
                }
            } else {
                current_branch_display_name = "HEAD (状态未知或损坏)";
            }
        } else {
            current_branch_display_name = "HEAD (空)";
        }
    } else {
        current_branch_display_name = "仓库尚未正确初始化或HEAD文件丢失";
    }
    std::cout << "当前分支: " << current_branch_display_name << std::endl;

    if (is_repository_empty && !head_commit_hash_opt) {
        std::cout << "\n尚无提交。" << std::endl;
    }

    bool in_merge_state = std::filesystem::exists(mygit_dir_ / MERGE_HEAD_FILE_NAME);
    std::unordered_set<std::filesystem::path> conflict_file_paths_set;
    if (in_merge_state) {
        std::cout << "提示: 您当前正处于一个合并过程中。" << std::endl;

        std::filesystem::path conflict_list_file = mygit_dir_ / FILE_CONFLICTS;
        if (std::filesystem::exists(conflict_list_file)) {
            std::ifstream cfl_ifs(conflict_list_file);
            std::string path_str;
            while (std::getline(cfl_ifs, path_str)) {
                if (!path_str.empty()) {
                    conflict_file_paths_set.insert(std::filesystem::path(path_str).lexically_normal());
                }
            }
            cfl_ifs.close();
        }
    }



    // --- 2. 加载 HEAD Commit 的文件树 (State 1) ---
    std::map<std::filesystem::path, std::pair<std::string, std::string>> head_commit_files_map;
    if (head_commit_hash_opt) {
        auto commit_opt = Commit::load_by_hash(*head_commit_hash_opt, get_objects_directory());
        if (commit_opt) {
            _load_tree_contents_recursive(commit_opt->tree_hash_hex, "", head_commit_files_map);
        } else {
            std::cerr << "警告 (status): 无法加载 HEAD commit 对象 " << *head_commit_hash_opt << std::endl;
            is_repository_empty = true; // 如果HEAD commit加载失败，视作仓库没有有效历史
        }
    } else { // 如果没有 head_commit_hash_opt，也说明仓库是空的或初始状态
        is_repository_empty = true;
    }


    // --- 3. 加载 Index (暂存区) 内容 (State 2) ---
    Index index_reader(mygit_dir_); // Index构造函数接收 .biogit 目录
    bool index_load_successful = index_reader.load();
    if (!index_load_successful && std::filesystem::exists(mygit_dir_ / Repository::INDEX_FILE_NAME)) { // INDEX_FILE_NAME 来自Repository
         std::cerr << "错误 (status): 加载索引失败，但索引文件存在。状态可能不准确。" << std::endl;
    }
    const auto& staged_entries_vec = index_reader.get_all_entries(); // 使用staged_entries_vec
    std::map<std::filesystem::path, const IndexEntry*> staged_files_map;
    for (const auto& entry : staged_entries_vec) {
        staged_files_map[entry.file_path] = &entry;
    }

    // --- 用于比较的列表 ---
    std::vector<std::pair<std::string, std::filesystem::path>> changes_to_be_committed;
    std::vector<std::pair<std::string, std::filesystem::path>> changes_not_staged;
    std::vector<std::filesystem::path> untracked_files_list; // 修正：之前叫untracked_files，这里统一

    // 收集所有在HEAD或Index中出现过的文件路径，用于后续识别未跟踪文件
    std::set<std::filesystem::path> all_known_paths_in_head_or_index;

    // --- 4.1 比较 Index vs HEAD ("Changes to be committed") ---
    for (const auto& staged_pair : staged_files_map) {
        const std::filesystem::path& rel_path = staged_pair.first;
        const IndexEntry* staged_entry = staged_pair.second;
        all_known_paths_in_head_or_index.insert(rel_path);

        auto head_it = head_commit_files_map.find(rel_path);
        if (head_it == head_commit_files_map.end()) {
            changes_to_be_committed.push_back({"新文件: ", rel_path});
        } else {
            if (staged_entry->blob_hash_hex != head_it->second.first || staged_entry->mode != head_it->second.second) {
                changes_to_be_committed.push_back({"修改:   ", rel_path});
            }
        }
    }
    for (const auto& head_pair : head_commit_files_map) {
        const std::filesystem::path& rel_path = head_pair.first;
        all_known_paths_in_head_or_index.insert(rel_path);
        if (staged_files_map.find(rel_path) == staged_files_map.end()) {
            changes_to_be_committed.push_back({"删除:   ", rel_path});
        }
    }

    // --- 4.2 遍历工作目录，比较 Working Directory vs Index ("Changes not staged" & "Untracked files") ---
    std::set<std::filesystem::path> files_found_in_work_tree;

    if (std::filesystem::exists(work_tree_root_) && std::filesystem::is_directory(work_tree_root_)) {
        std::filesystem::recursive_directory_iterator dir_iter(
            work_tree_root_,
            std::filesystem::directory_options::skip_permission_denied,
            ec
        );
        if (ec) {
            std::cerr << "错误 (status): 无法遍历工作目录 '" << work_tree_root_.string() << "': " << ec.message() << std::endl;
        } else {
            std::filesystem::recursive_directory_iterator end_iter;
            for (; dir_iter != end_iter; ++dir_iter) {
                std::error_code entry_ec;
                const auto& current_dir_entry_obj = *dir_iter;
                const std::filesystem::path& current_abs_path_from_iterator = current_dir_entry_obj.path(); // 使用新变量名

                // 跳过 .biogit 目录
                // 确保比较的是规范化后的路径
                if (current_abs_path_from_iterator.lexically_normal().string().rfind(mygit_dir_.lexically_normal().string(), 0) == 0) {
                    if (dir_iter.depth() == 0 && current_abs_path_from_iterator.filename() == MYGIT_DIR_NAME) {
                         dir_iter.disable_recursion_pending();
                    }
                    continue;
                }

                if (current_dir_entry_obj.is_regular_file(entry_ec)) {
                    std::filesystem::path current_file_abs_path = current_abs_path_from_iterator.lexically_normal(); // 使用局部规范化路径
                    std::filesystem::path rel_path = std::filesystem::relative(current_file_abs_path, work_tree_root_, ec).lexically_normal();

                    if (ec || rel_path.empty() || rel_path.string().rfind("..",0) == 0) continue;

                    files_found_in_work_tree.insert(rel_path);

                    auto staged_it = staged_files_map.find(rel_path);
                    if (staged_it != staged_files_map.end()) {
                        const IndexEntry* staged_entry = staged_it->second;
                        bool metadata_differs = false;

                        auto ftime_workdir = std::filesystem::last_write_time(current_file_abs_path, ec);
                        if (ec) { metadata_differs = true; }
                        else {
                            auto file_time_duration_since_epoch = ftime_workdir.time_since_epoch();
                            auto system_clock_compatible_duration =
                                std::chrono::duration_cast<std::chrono::system_clock::duration>(file_time_duration_since_epoch);
                            std::chrono::system_clock::time_point mtime_workdir(system_clock_compatible_duration);
                            if (mtime_workdir != staged_entry->mtime) {
                                metadata_differs = true;
                            }
                        }

                        uintmax_t size_workdir = std::filesystem::file_size(current_file_abs_path, ec);
                        if (ec) { metadata_differs = true; }
                        else if (size_workdir != staged_entry->file_size) {
                            metadata_differs = true;
                        }

                        if (metadata_differs) {
                            std::ifstream ifs(current_file_abs_path, std::ios::binary);
                            std::vector<std::byte> content;
                            if(ifs.is_open()){
                                ifs.seekg(0, std::ios::end);
                                std::streamsize file_size_check = ifs.tellg();
                                ifs.seekg(0, std::ios::beg);
                                if (file_size_check > 0) {
                                    content.resize(static_cast<size_t>(file_size_check));
                                    if (!ifs.read(reinterpret_cast<char*>(content.data()), file_size_check)) {
                                        content.clear();
                                    }
                                }
                                ifs.close();
                            }

                            if (!content.empty() || (std::filesystem::exists(current_file_abs_path) && std::filesystem::file_size(current_file_abs_path)==0) ) {
                                Blob temp_blob(content);
                                std::string workdir_blob_hash = SHA1::sha1(temp_blob.serialize());
                                if (workdir_blob_hash != staged_entry->blob_hash_hex) {
                                    changes_not_staged.push_back({"修改:   ", rel_path});
                                }
                            } else if (std::filesystem::exists(current_file_abs_path)) {
                                changes_not_staged.push_back({"错误读取:", rel_path});
                            }
                        }
                    }
                    // Untracked files will be determined later by set difference
                } else if (entry_ec) {
                     std::cerr << "警告 (status): 检查工作区路径 '" << current_abs_path_from_iterator.string() // 使用 current_abs_path_from_iterator
                              << "' 类型时出错: " << entry_ec.message() << std::endl;
                }
            }
        }
    }

    // 4.3 检查 Index 中有但工作目录中没有的文件
    for (const auto& staged_pair : staged_files_map) {
        const std::filesystem::path& rel_path = staged_pair.first;
        if (files_found_in_work_tree.find(rel_path) == files_found_in_work_tree.end()) {
            changes_not_staged.push_back({"删除:   ", rel_path});
        }
    }

    // 4.4 找出真正的未跟踪文件: 在工作目录中，但既不在HEAD也不在Index中
    for(const auto& work_tree_file_rel_path : files_found_in_work_tree) {
        // all_known_paths_in_head_or_index 包含了所有在 HEAD 或 Index 中已知的路径
        if (all_known_paths_in_head_or_index.find(work_tree_file_rel_path) == all_known_paths_in_head_or_index.end()) {
            untracked_files_list.push_back(work_tree_file_rel_path);
        }
    }

    // 排序各个列表以便显示
    std::sort(changes_to_be_committed.begin(), changes_to_be_committed.end());
    std::sort(changes_not_staged.begin(), changes_not_staged.end());
    std::sort(untracked_files_list.begin(), untracked_files_list.end());


    // --- 5. 打印状态信息 ---
    if (is_repository_empty && changes_to_be_committed.empty() && changes_not_staged.empty() && untracked_files_list.empty()) {
        std::cout << "无内容可添加，无内容可提交。" << std::endl;
        std::cout << "(创建/拷贝文件并使用 \"biogit add\" 来跟踪它们)" << std::endl;
        return;
    }

    if (changes_to_be_committed.empty() && changes_not_staged.empty() && untracked_files_list.empty()) {
        std::cout << "\n无内容可提交，工作区干净。" << std::endl;
        return;
    }

    if (!changes_to_be_committed.empty()) {
        std::cout << "\n要提交的更改:" << std::endl;
        std::cout << "  (使用 \"biogit rm --cached <文件>...\" 来取消暂存)" << std::endl;
        for (const auto& change : changes_to_be_committed) {
            std::cout << "\t" << "\033[32m" << change.first << "\t" << change.second.generic_string() << "\033[0m" << std::endl;
        }
    }

    if (!changes_not_staged.empty()) {
        if (in_merge_state) {
            std::cout << "\n尚未暂存以备提交的更改:" << std::endl;
            std::cout << "  (使用 \"biogit add <文件>...\" 来更新要提交的内容)" << std::endl;
            for (const auto& change : changes_not_staged) {
                if (conflict_file_paths_set.count(change.second.lexically_normal())) {
                    std::cout << "\t" << "\033[31m" << "冲突:   " << "\t" << change.second.generic_string() << "\033[0m" << std::endl;
                }else {
                    std::cout << "\t" << "\033[31m" << change.first << "\t" << change.second.generic_string() << "\033[0m" << std::endl;
                }
            }

        }else {
            std::cout << "\n尚未暂存以备提交的更改:" << std::endl;
            std::cout << "  (使用 \"biogit add <文件>...\" 来更新要提交的内容)" << std::endl;
            for (const auto& change : changes_not_staged) {
                std::cout << "\t" << "\033[31m" << change.first << "\t" << change.second.generic_string() << "\033[0m" << std::endl;
            }
        }
    }


    if (!untracked_files_list.empty()) {
        std::cout << "\n未跟踪的文件:" << std::endl;
        std::cout << "  (使用 \"biogit add <文件>...\" 来包含要提交的内容)" << std::endl;
        for (const auto& file_path : untracked_files_list) {
            std::cout << "\t" << "\033[31m" << file_path.generic_string() << "\033[0m" << std::endl;
        }
    }

    // 使用 staged_entries_vec 替换未定义的 index_entries
    if (changes_to_be_committed.empty()) { // 如果没有暂存的更改
        if (!staged_entries_vec.empty() && !is_repository_empty) {
             // 索引非空 (意味着它与HEAD一致)，且不是初始仓库
             if (!changes_not_staged.empty() || !untracked_files_list.empty()){
                std::cout << "\n无更改被添加到提交，但工作区存在修改或未跟踪文件。" << std::endl;
             } // else 工作区也干净，这个情况已在上面 "工作区干净" 处理
        } else if (staged_entries_vec.empty() && !is_repository_empty) {
            // 索引为空，且不是初始仓库 (例如 commit 后索引被清空)
             if (!changes_not_staged.empty() || !untracked_files_list.empty()) {
                std::cout << "\n无内容可提交 (使用 \"biogit add\" 来跟踪或暂存文件)。" << std::endl;
             } // else 工作区也干净，这个情况已在上面 "工作区干净" 处理
        }
        // 如果 is_repository_empty 且索引为空，上面的初始提示已处理
    }
    std::cout << std::endl;
}


void Repository::log() const {
    // 1. 获取当前 HEAD 指向的 Commit 哈希
    std::optional<std::string> current_commit_hash_opt = _get_head_commit_hash();

    if (!current_commit_hash_opt) { // 无法确定 HEAD Commit 哈希
        std::cout << "尚无提交历史。" << std::endl;
        // 检查是否是因为 HEAD 指向一个不存在的分支
        std::filesystem::path head_path = get_head_file_path();
        if (std::filesystem::exists(head_path)) {
            std::ifstream head_ifs(head_path);
            std::string head_content_line;
            if (std::getline(head_ifs, head_content_line)) {
                // 去掉多余空格
                if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) { head_content_line.pop_back(); }
                if (!head_content_line.empty() && head_content_line.back() == '\r') { head_content_line.pop_back(); }

                if (head_content_line.rfind("ref: refs/heads/", 0) == 0) {
                    std::string branch_ref = head_content_line.substr(std::string("ref: refs/heads/").length());
                    if (!std::filesystem::exists(mygit_dir_ / ("refs/heads/" + branch_ref))) {
                        std::cout << "当前分支 '" << branch_ref << "' 尚无提交。" << std::endl;
                    }
                }
            }
        }
        return;
    }

    std::string commit_to_log_hash = *current_commit_hash_opt;
    int commit_count = 0; // 用于控制输出数量（可选）
    const int MAX_LOG_ENTRIES = 50; // 示例：最多显示50条日志

    // 2. 循环追溯并打印父 Commit
    while (!commit_to_log_hash.empty() && commit_count < MAX_LOG_ENTRIES) {
        // a. 加载 Commit 对象
        auto commit_opt = Commit::load_by_hash(commit_to_log_hash, get_objects_directory());
        if (!commit_opt) {
            std::cerr << "错误 (log): 无法加载 Commit 对象 " << commit_to_log_hash << std::endl;
            break; // 中断日志追溯
        }
        const Commit& current_commit = *commit_opt;

        // b. 打印 Commit 信息
        std::cout << "\033[33mcommit " << commit_to_log_hash << "\033[0m" << std::endl; // 黄色显示 commit 哈希

        // 打印作者信息
        std::time_t author_time_t = std::chrono::system_clock::to_time_t(current_commit.author.timestamp);
        std::tm author_tm = *std::localtime(&author_time_t); // 转换为本地时间结构
        std::cout << "Author: " << current_commit.author.name << " <" << current_commit.author.email << ">" << std::endl;
        // 格式化日期：例如 "Mon May 20 15:30:00 2024 +0800"
        char date_buf[100];
        if (std::strftime(date_buf, sizeof(date_buf), "%a %b %d %H:%M:%S %Y %z", &author_tm)) {
            std::cout << "Date:   " << date_buf << std::endl;
        } else {
            std::cout << "Date:   <无法格式化日期>" << std::endl;
        }

        std::cout << std::endl; // 空行
        // 打印提交信息 (需要处理多行，并添加缩进)
        std::istringstream message_stream(current_commit.message);
        std::string message_line;
        while (std::getline(message_stream, message_line)) {
            std::cout << "    " << message_line << std::endl;
        }
        std::cout << std::endl; // 在每个 commit 条目后加一个空行

        // c. 获取父 Commit 哈希 (简化版：只追溯第一个父提交)
        if (!current_commit.parent_hashes_hex.empty()) {
            commit_to_log_hash = current_commit.parent_hashes_hex[0]; // 取第一个父提交
        } else {
            commit_to_log_hash.clear(); // 没有父提交了（到达根提交）
        }
        commit_count++;
    } // end while

    if (commit_count == 0 && current_commit_hash_opt) {
        std::cout << "无法显示提交历史（无法加载起始提交）。" << std::endl;
    } else if (commit_count >= MAX_LOG_ENTRIES) {
        std::cout << "...\n(已达到最大日志显示数量)" << std::endl;
    }
}


/**
 * @brief 创建一个新的本地分支。
 * @param branch_name 新分支的名称。
 * @param start_point_commit_hash_param (可选) 新分支基于的Commit的SHA-1哈希。
 * 如果为空，则基于当前HEAD指向的Commit创建。
 * @return 如果分支创建成功，返回 true；否则返回 false (例如分支已存在，或起点Commit无效)。
 */
bool Repository::branch_create(const std::string& branch_name, const std::string& start_point_commit_hash_param) {
    // 检查分支名称是否有效
    if (branch_name.empty() || branch_name.find('/') != std::string::npos || branch_name == "HEAD") {
        std::cerr << "错误: 无效的分支名称 '" << branch_name << "'" << std::endl;
        return false;
    }

    std::filesystem::path new_branch_file_path = get_heads_directory() / branch_name;
    std::error_code ec;

    if (std::filesystem::exists(new_branch_file_path, ec)) {
        std::cerr << "错误: 分支 '" << branch_name << "' 已存在。" << std::endl;
        return false;
    }
    if (ec) {
        std::cerr << "错误: 检查分支文件 '" << new_branch_file_path.string() << "' 是否存在时出错: " << ec.message() << std::endl;
        return false;
    }

    std::string target_commit_hash;

    if (!start_point_commit_hash_param.empty()) {
        // 如果用户指定了起点 Commit 哈希
        std::optional<string> full_hash_ptr = _resolve_commit_ish_to_full_hash(start_point_commit_hash_param);

        if (full_hash_ptr) { // 基本的哈希长度检查
            // 验证 commit_hash 是否真的存在于对象库中
            string full_hash = *full_hash_ptr;
            auto commit_opt = Commit::load_by_hash(full_hash, get_objects_directory());
            if (!commit_opt) {
                std::cerr << "错误: 起点Commit '" << full_hash<< "' 未找到。" << std::endl;
                return false;
            }
            target_commit_hash = full_hash;
        } else {
            std::cerr << "错误: 无效的起点Commit哈希格式: " << start_point_commit_hash_param << std::endl;
            return false;
        }
    } else {
        // 如果没有指定起点，则基于当前 HEAD 指向的 Commit 创建
        std::optional<std::string> head_commit_opt = _get_head_commit_hash(); // _get_head_commit_hash 在 repository.cpp 中
        if (!head_commit_opt) {
            std::cerr << "错误: 无法获取当前HEAD的Commit。仓库可能还没有任何提交，或者HEAD无效。" << std::endl;
            return false;
        }
        target_commit_hash = *head_commit_opt;
    }

    // 创建分支引用文件并写入 Commit 哈希
    std::filesystem::create_directories(new_branch_file_path.parent_path(), ec);
    if (ec) {
        std::cerr << "错误: 无法创建 refs/heads 目录: " << ec.message() << std::endl;
        return false;
    }

    std::ofstream branch_ofs(new_branch_file_path);
    if (!branch_ofs.is_open()) {
        std::cerr << "错误: 无法创建分支文件 '" << new_branch_file_path.string() << "'" << std::endl;
        return false;
    }
    // Git 通常会在引用文件末尾加换行
    branch_ofs << target_commit_hash << std::endl;
    branch_ofs.close();

    if (!branch_ofs.good()) { // 检查写入是否真的成功
        std::cerr << "错误: 写入分支文件 '" << new_branch_file_path.string() << "' 失败。" << std::endl;
        // 尝试删除未成功写入的文件
        std::filesystem::remove(new_branch_file_path, ec);
        return false;
    }

    std::cout << "已创建分支 '" << branch_name << "'，指向 Commit " << target_commit_hash.substr(0, 7) << std::endl;
    return true;
}


/**
 * @brief 列出所有本地分支，并高亮显示当前活动分支。
 * 此方法为 const，因为它不修改仓库状态。
 */
void Repository::branch_list() const {
    std::filesystem::path heads_dir = get_heads_directory();
    std::string current_branch_name_from_head;
    bool head_is_detached = false;

    // 1. 读取 HEAD 文件以确定当前分支或状态
    std::filesystem::path head_file_path = get_head_file_path();
    if (std::filesystem::exists(head_file_path)) {
        std::ifstream head_ifs(head_file_path);
        std::string head_content_line;
        if (std::getline(head_ifs, head_content_line)) {
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) { head_content_line.pop_back(); }
            if (!head_content_line.empty() && head_content_line.back() == '\r') { head_content_line.pop_back(); }

            if (head_content_line.rfind("ref: refs/heads/", 0) == 0) {
                current_branch_name_from_head = head_content_line.substr(std::string("ref: refs/heads/").length());
            } else if (head_content_line.length() == 40) {
                head_is_detached = true;
            }
        }
    }

    // 2. 遍历 refs/heads 目录
    std::set<std::string> branch_names; // 使用 set 来自动排序
    std::error_code ec;
    if (std::filesystem::exists(heads_dir, ec) && std::filesystem::is_directory(heads_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(heads_dir, ec)) {
            if (entry.is_regular_file(ec)) { // 分支引用应该是常规文件
                branch_names.insert(entry.path().filename().string());
            } else if (ec) {
                 std::cerr << "警告 (branch_list): 检查路径 '" << entry.path().string() << "' 类型时出错: " << ec.message() << std::endl;
            }
        }
        if (ec) { // directory_iterator 构造或迭代中的错误
            std::cerr << "错误 (branch_list): 遍历分支目录 '" << heads_dir.string() << "' 失败: " << ec.message() << std::endl;
            return;
        }
    } else {
        std::cout << "  (没有分支)" << std::endl;
        return;
    }

    if (branch_names.empty()) {
        std::cout << "  (没有分支)" << std::endl;
        return;
    }

    // 3. 打印分支列表
    for (const auto& branch_name : branch_names) {
        if (!head_is_detached && branch_name == current_branch_name_from_head) {
            std::cout << "\033[32m* " << branch_name << "\033[0m" << std::endl; // 绿色高亮当前分支
        } else {
            std::cout << "  " << branch_name << std::endl;
        }
    }

    // 分离头指针状态
    if (head_is_detached) {
        std::cout << "* (HEAD 处于分离状态，未指向任何分支)" << std::endl;
    }
}


/**
 * @brief 删除一个本地分支。
 * @param branch_name 要删除的分支的名称。
 * @param force (可选) 是否强制删除。如果为 true，则类似于 "git branch -D"。
 * 如果为 false (默认)，则类似于 "git branch -d"，可能会进行一些安全检查 (简化版中可能不检查合并状态)。
 * @return 如果分支成功删除，返回 true；否则返回 false (例如，分支不存在，或试图删除当前分支等)。
 */
bool Repository::branch_delete(const std::string& branch_name, bool force) {
    // 1. 检查分支名称是否有效 (与 branch_create 中的验证类似)
    if (branch_name.empty() || branch_name.find('/') != std::string::npos || branch_name == "HEAD") {
        std::cerr << "错误: 无效的分支名称 '" << branch_name << "'" << std::endl;
        return false;
    }

    // 2. 构造分支引用文件的路径
    std::filesystem::path branch_file_path = get_heads_directory() / branch_name;
    std::error_code ec;

    // 3. 检查分支是否存在
    if (!std::filesystem::exists(branch_file_path, ec) || !std::filesystem::is_regular_file(branch_file_path, ec)) {
        std::cerr << "错误: 分支 '" << branch_name << "' 未找到。" << std::endl;
        return false;
    }
    if (ec) {
        std::cerr << "错误: 检查分支文件 '" << branch_file_path.string() << "' 时出错: " << ec.message() << std::endl;
        return false;
    }

    // 4. 检查是否试图删除当前活动分支
    std::filesystem::path head_path = get_head_file_path();
    if (std::filesystem::exists(head_path)) {
        std::ifstream head_ifs(head_path);
        std::string head_content_line;
        if (std::getline(head_ifs, head_content_line)) {
            // 清理行尾可能的换行符
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) {
                head_content_line.pop_back();
            }
            if (!head_content_line.empty() && head_content_line.back() == '\r') {
                head_content_line.pop_back();
            }

            std::string current_ref_in_head = "refs/heads/" + branch_name;
            if (head_content_line == ("ref: " + current_ref_in_head)) {
                std::cerr << "错误: 不能删除当前所在的分支 '" << branch_name << "'，即使是强制模式。" << std::endl;
                std::cerr << "请先切换到其他分支。" << std::endl;
                return false;
            }
        }
        head_ifs.close();
    }

    // 5. (简化版：跳过合并状态检查)

    // 6. 删除分支引用文件
    if (std::filesystem::remove(branch_file_path, ec)) {
        std::cout << "已删除分支 '" << branch_name << std::endl;
        return true;
    } else {
        std::cerr << "错误: 删除分支文件 '" << branch_file_path.string() << "' 失败: " << ec.message() << std::endl;
        return false;
    }
}


/**
 * @brief 切换到指定的目标分支或检出指定的 commit (分离 HEAD)
 * \n 要求在切换前，工作目录和索引相对于当前HEAD是干净的。
 * \n 切换成功后，HEAD、索引和工作目录都将反映目标分支的状态。
 * @param target_identifier 要切换到的分支名称、commit 哈希、标签名等
 * @return 如果成功切换，返回 true；否则返回 false。
 */
bool Repository::switch_branch(const std::string& target_identifier) {
    std::error_code ec;

    // 0. 检查工作区是否干净 (与原逻辑一致)
    if (!is_workspace_clean()) { //
        std::cerr << "错误: 你有尚未提交的本地更改。" << std::endl;
        std::cerr << "请在切换分支前提交或储藏你的更改。" << std::endl;
        return false;
    }

    std::filesystem::path head_file_path = get_head_file_path();
    std::string target_commit_hash;      // 将用于存储最终要检出的 commit 的哈希
    bool is_switching_to_branch = false; // 标记是切换到分支还是分离 HEAD
    std::string target_branch_name_for_ref; // 如果是切换分支，存储分支名以更新 HEAD

    // 1. 尝试将 target_identifier 解析为分支名
    std::filesystem::path potential_branch_file_path = get_heads_directory() / target_identifier;
    if (std::filesystem::exists(potential_branch_file_path, ec) && std::filesystem::is_regular_file(potential_branch_file_path, ec)) {
        // target_identifier 是一个已存在的分支名
        std::ifstream branch_file_ifs(potential_branch_file_path);
        std::string commit_hash_from_branch;
        if (!(branch_file_ifs >> commit_hash_from_branch) || commit_hash_from_branch.length() != 40) {
            std::cerr << "错误: 无法读取分支 '" << target_identifier << "' 的有效 Commit 哈希。" << std::endl;
            return false;
        }
        branch_file_ifs.close();

        target_commit_hash = commit_hash_from_branch;
        is_switching_to_branch = true;
        target_branch_name_for_ref = target_identifier;

        // 检查是否已在当前分支且 commit 一致
        std::string current_head_content_str;
        if (std::filesystem::exists(head_file_path)) {
            std::ifstream h_ifs(head_file_path);
            std::getline(h_ifs, current_head_content_str); // 读取整行
            h_ifs.close();
             // 清理行尾换行符
            if (!current_head_content_str.empty() && (current_head_content_str.back() == '\n' || current_head_content_str.back() == '\r')) { current_head_content_str.pop_back(); }
            if (!current_head_content_str.empty() && current_head_content_str.back() == '\r') { current_head_content_str.pop_back(); }
        }
        std::string expected_ref_str = "ref: refs/heads/" + target_identifier;
        if (current_head_content_str == expected_ref_str) {
            std::cout << "已在分支 '" << target_identifier << "'" << std::endl;
            return true;
        }

    } else if (ec) { // 检查分支文件是否存在时发生错误
        std::cerr << "错误: 检查目标 '" << target_identifier << "' 是否为分支时出错: " << ec.message() << std::endl;
        return false;
    } else {
        // target_identifier 不是一个已存在的分支名，尝试将其解析为 commit-ish (哈希、标签等)
        std::optional<std::string> resolved_commit_hash_opt = _resolve_commit_ish_to_full_hash(target_identifier);
        if (!resolved_commit_hash_opt) {
            std::cerr << "错误: '" << target_identifier << "' 不是一个有效的分支、commit 或标签。" << std::endl;
            return false;
        }
        target_commit_hash = *resolved_commit_hash_opt;
        is_switching_to_branch = false; // 将进入分离 HEAD 状态

        // 检查 HEAD 是否已分离并指向此 commit
        std::string current_head_content_str;
        if (std::filesystem::exists(head_file_path)) {
             std::ifstream h_ifs(head_file_path);
             std::getline(h_ifs, current_head_content_str);
             h_ifs.close();
             // 清理行尾换行符
            if (!current_head_content_str.empty() && (current_head_content_str.back() == '\n' || current_head_content_str.back() == '\r')) { current_head_content_str.pop_back(); }
            if (!current_head_content_str.empty() && current_head_content_str.back() == '\r') { current_head_content_str.pop_back(); }
        }
        if (current_head_content_str == target_commit_hash) { // 直接比较是否为目标哈希
            std::cout << "HEAD 已分离并指向 " << target_commit_hash.substr(0, 7) << std::endl;
            return true;
        }
    }
    // 此处，target_commit_hash 应该是有效的，is_switching_to_branch 也已设置

    // 2. 加载目标 Commit 对象并获取其根 Tree 哈希
    auto target_commit_opt = Commit::load_by_hash(target_commit_hash, get_objects_directory()); //
    if (!target_commit_opt) {
        std::cerr << "错误: 无法加载目标 Commit 对象 '" << target_commit_hash.substr(0,7) << "'。" << std::endl;
        return false;
    }
    std::string target_root_tree_hash = target_commit_opt->tree_hash_hex;

    // 3. 获取当前 HEAD Commit 的文件列表 (用于 _update_working_directory_from_tree)
    std::map<std::filesystem::path, std::pair<std::string, std::string>> current_repo_files_map;
    auto current_actual_head_commit_hash_opt = _get_head_commit_hash(); // 获取当前 HEAD 实际指向的 commit
    if (current_actual_head_commit_hash_opt) {
        auto current_commit_obj_opt = Commit::load_by_hash(*current_actual_head_commit_hash_opt, get_objects_directory());
        if (current_commit_obj_opt) {
            _load_tree_contents_recursive(current_commit_obj_opt->tree_hash_hex, "", current_repo_files_map); //
        }
    }

    // 4. 更新工作目录以匹配目标 Tree
    if (!_update_working_directory_from_tree(target_root_tree_hash, current_repo_files_map)) { //
        std::cerr << "错误: 更新工作目录以匹配目标 '" << target_identifier << "' 失败。" << std::endl;
        return false;
    }

    // 5. 更新索引以匹配目标 Tree
    index_manager_.clear_in_memory(); //
    _populate_index_from_tree_recursive(target_root_tree_hash, "", index_manager_); //
    if (!index_manager_.write()) { //
        std::cerr << "严重错误: 更新索引文件以匹配目标 '" << target_identifier << "' 失败！" << std::endl;
        return false;
    }

    // 6. 更新 HEAD 文件
    std::ofstream head_ofs(head_file_path, std::ios::trunc);
    if (!head_ofs.is_open()) {
        std::cerr << "严重错误: 无法打开 HEAD 文件进行更新！" << std::endl;
        return false;
    }

    if (is_switching_to_branch) {
        head_ofs << "ref: refs/heads/" << target_branch_name_for_ref << std::endl;
        head_ofs.close();
        std::cout << "已切换到分支 '" << target_branch_name_for_ref << "'" << std::endl;
    } else { // 进入分离 HEAD 状态
        head_ofs << target_commit_hash << std::endl;
        head_ofs.close();
        std::cout << "HEAD 现在处于分离状态，指向 " << target_commit_hash.substr(0, 7) << std::endl;
        std::cout << "您可以在此状态下进行实验性提交。若要保留这些提交，" << std::endl;
        std::cout << "请在切换到其他分支前，使用 'biogit branch <新分支名> "
                  << target_commit_hash.substr(0,7) << "' 创建一个新分支指向它。" << std::endl;
    }

    return true;
}


/**
 * @brief 显示不同版本之间文件内容的具体差异
 * @param options ： DiffOptions类型
 * @details
 *       biogit diff commit <hash1> <hash2> [<path>...]
 *       biogit diff branch <branch1> <branch2> [<path>...]
 *       biogit diff (默认：工作目录 vs 索引，可带路径参数 [<path>...])
 *       biogit diff --staged [<path>...]
 */
void Repository::diff(const DiffOptions& options) {
    // 确保索引文件已加载，后续操作依赖索引信息
    if (!index_manager_.is_loaded()) {
        if (!index_manager_.load() && std::filesystem::exists(get_index_file_path())) { //
            std::cerr << "错误: diff 操作前，索引文件加载失败。" << std::endl;
            return;
        }
    }

    // --- 模式选择 ---

    // 模式 3: 比较两个指定的 Commit / 分支
    if (!options.commit1_hash_str.empty() && !options.commit2_hash_str.empty()) {
        // 将用户输入的 commit 标识符 (可能是分支名、部分哈希等) 解析为完整的 40 位 SHA-1 哈希
        std::optional<std::string> full_hash1_opt = _resolve_commit_ish_to_full_hash(options.commit1_hash_str);
        std::optional<std::string> full_hash2_opt = _resolve_commit_ish_to_full_hash(options.commit2_hash_str);

        // 检查解析是否成功
        if (!full_hash1_opt) { std::cerr << "错误: 无法解析 commit/branch '" << options.commit1_hash_str << "'" << std::endl; return; }
        if (!full_hash2_opt) { std::cerr << "错误: 无法解析 commit/branch '" << options.commit2_hash_str << "'" << std::endl; return; }

        std::string full_hash1 = *full_hash1_opt;
        std::string full_hash2 = *full_hash2_opt;

        // 如果两个 commit 哈希相同，则它们指向同一个 commit
        if (full_hash1 == full_hash2) {
             if (options.paths_to_diff.empty()) {
                // 如果没有指定特定文件路径，则提示两个 commit 相同
                std::cout << "比较的两个 commit 相同: " << full_hash1.substr(0,7) << std::endl;
             } else {
                 // 如果指定了路径，理论上应该检查这些路径在该 commit 中是否存在，
                 // 但由于 commit 相同，这些路径之间不会有差异。
                 // 为简化，此处不为相同 commit 且指定路径的情况输出特定 diff 内容。
             }
            return;
        }

        // 加载两个 commit 对象
        auto commit1_obj_opt = Commit::load_by_hash(full_hash1, get_objects_directory());
        auto commit2_obj_opt = Commit::load_by_hash(full_hash2, get_objects_directory());

        if (!commit1_obj_opt) { std::cerr << "错误: 无法加载 commit '" << options.commit1_hash_str << "' (resolved to " << full_hash1.substr(0,7) << ")" << std::endl; return; }
        if (!commit2_obj_opt) { std::cerr << "错误: 无法加载 commit '" << options.commit2_hash_str << "' (resolved to " << full_hash2.substr(0,7) << ")" << std::endl; return; }

        // 加载两个 commit 各自根树的内容到 map 中
        // map 结构: 文件相对路径 -> {blob 哈希, 文件模式}
        std::map<std::filesystem::path, std::pair<std::string, std::string>> files_map1;
        _load_tree_contents_recursive(commit1_obj_opt->tree_hash_hex, "", files_map1); //

        std::map<std::filesystem::path, std::pair<std::string, std::string>> files_map2;
        _load_tree_contents_recursive(commit2_obj_opt->tree_hash_hex, "", files_map2);

        // 收集需要处理的路径集合
        std::set<std::filesystem::path> paths_to_process;
        if (options.paths_to_diff.empty()) { // 如果用户没有指定路径，则比较两个 commit 间所有涉及的文件
            for (const auto& pair : files_map1) paths_to_process.insert(pair.first);
            for (const auto& pair : files_map2) paths_to_process.insert(pair.first);
        } else { // 如果用户指定了路径，则只处理这些路径
            for (const auto& p_user_input : options.paths_to_diff) {
                // 将用户输入的路径（可能是绝对或相对当前工作目录）转换为相对于仓库根的路径
                std::optional<std::filesystem::path> rel_p_opt = normalize_and_relativize_path(p_user_input);
                if (rel_p_opt) {
                    const auto& user_spec_path = *rel_p_opt; // 使用规范化后的路径进行比较
                    bool found_matching_content = false;
                    // 检查是否直接存在于任一 commit 的文件列表中 (作为文件)
                    if (files_map1.count(user_spec_path) || files_map2.count(user_spec_path)) {
                         paths_to_process.insert(user_spec_path);
                         found_matching_content = true; // 标记直接找到了文件
                    }

                    // 输入的可能是一个目录， 判断pair.first 是否在 user_spec_path路径下
                    for(const auto& pair : files_map1) { // 检查 commit1
                        // 如果 pair.first 在 user_spec_path 目录下 (且不等于它自身，除非 user_spec_path 确实是个文件且已加入)
                        if (pair.first != user_spec_path && Utils::is_path_under_or_equal(pair.first, user_spec_path)) {
                            paths_to_process.insert(pair.first);
                            found_matching_content = true;
                        }
                    }
                    for(const auto& pair : files_map2) { // 检查 commit2
                        if (pair.first != user_spec_path && Utils::is_path_under_or_equal(pair.first, user_spec_path)) {
                            paths_to_process.insert(pair.first);
                            found_matching_content = true;
                        }
                    }
                    // 如果既不是文件也不是有效目录前缀 (即遍历后 found_matching_content 仍为 false)
                    if (!found_matching_content) {
                         std::cout << "提示: 路径 '" << p_user_input.string() << "' (规范化为 '" << user_spec_path.string() << "') 在比较的 commit 中未作为文件或目录查找到。" << std::endl;
                    }
                } else { // 路径规范化失败
                    std::cout << "警告: 无法规范化路径 '" << p_user_input.string() << "'，已跳过。" << std::endl;
                }
            }
        }

        // 构建用于 diff 输出的标签后缀，通常是 commit 标识符的前几位
        std::string label1_suffix = " (" + options.commit1_hash_str.substr(0, std::min((size_t)7, options.commit1_hash_str.length())) + ")";
        std::string label2_suffix = " (" + options.commit2_hash_str.substr(0, std::min((size_t)7, options.commit2_hash_str.length())) + ")";

        // 遍历所有需要处理的路径，进行比较
        for (const auto& path : paths_to_process) {
            auto it1 = files_map1.find(path);
            auto it2 = files_map2.find(path);
            // 获取各自的 blob 哈希，如果文件不存在于某个 commit 中，则哈希为空字符串
            std::string blob_hash1 = (it1 != files_map1.end()) ? it1->second.first : "";
            std::string blob_hash2 = (it2 != files_map2.end()) ? it2->second.first : "";

            // 如果两个 blob 哈希相同 (包括都为空的情况，即文件在两边都不存在于此路径下)，
            // 则内容相同，无需 diff
            if (blob_hash1 == blob_hash2) continue;

            // 获取两个版本的行内容。如果 blob_hash 为空，则视为空文件内容。
            auto lines1_opt = blob_hash1.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(blob_hash1);
            auto lines2_opt = blob_hash2.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(blob_hash2);

            // 确保能成功加载两边的内容（即使是空内容）才进行 diff
            if (lines1_opt && lines2_opt) {
                _perform_and_print_file_diff(path, *lines1_opt, label1_suffix, *lines2_opt, label2_suffix); //
            }
        }

    } else if (options.staged) {
        // --- 模式 2: Index vs HEAD ---
        std::optional<std::string> head_commit_hash_opt = _get_head_commit_hash(); //
        std::map<std::filesystem::path, std::pair<std::string, std::string>> head_files_map; // path -> {blob_hash, mode}
        if (head_commit_hash_opt) {
            auto commit_opt = Commit::load_by_hash(*head_commit_hash_opt, get_objects_directory());
            if (commit_opt) {
                _load_tree_contents_recursive(commit_opt->tree_hash_hex, "", head_files_map);
            } else {
                std::cerr << "警告 (diff --staged): 无法加载 HEAD commit " << *head_commit_hash_opt << std::endl;
            }
        }
        // 如果没有 HEAD commit (例如新仓库首次提交前), head_files_map 会为空。

        const auto& index_entries = index_manager_.get_all_entries();
        std::set<std::filesystem::path> paths_to_process;
        std::map<std::filesystem::path, std::string> index_files_map; // path -> blob_hash

        // 确定要处理的路径集合
        if (options.paths_to_diff.empty()) { // Diff所有涉及的文件
            for (const auto& entry : index_entries) {
                paths_to_process.insert(entry.file_path);
                index_files_map[entry.file_path] = entry.blob_hash_hex;
            }
            for (const auto& pair : head_files_map) {
                paths_to_process.insert(pair.first);
            }
        } else { // Diff 用户指定的路径
            for (const auto& p_user_input : options.paths_to_diff) {
                std::optional<std::filesystem::path> rel_p_opt = normalize_and_relativize_path(p_user_input);
                if (rel_p_opt) {
                    const auto& user_spec_path = *rel_p_opt;
                    bool path_found_for_current_spec = false; // 用于判断此 p_user_input 是否找到了任何匹配项
                    // 检查是否为 Index 中的文件 (直接匹配)
                    auto idx_entry_it = std::find_if(index_entries.begin(), index_entries.end(),
                                                     [&](const IndexEntry& e){ return e.file_path == user_spec_path; });
                    if (idx_entry_it != index_entries.end()) {
                        paths_to_process.insert(user_spec_path);
                        index_files_map[user_spec_path] = idx_entry_it->blob_hash_hex;
                        path_found_for_current_spec = true;
                    }
                    // 检查是否为 HEAD 中的文件 (直接匹配)
                    if (head_files_map.count(user_spec_path)) {
                        paths_to_process.insert(user_spec_path);
                        path_found_for_current_spec = true;
                    }

                    // 尝试作为目录前缀
                    for(const auto& entry : index_entries) {
                        // entry.file_path != user_spec_path 是为了避免重复添加已被直接匹配的项
                        // is_path_under_or_equal 会检查 entry.file_path 是否等于 user_spec_path 或在其下
                        if (entry.file_path != user_spec_path && Utils::is_path_under_or_equal(entry.file_path, user_spec_path)) {
                            paths_to_process.insert(entry.file_path);
                            index_files_map[entry.file_path] = entry.blob_hash_hex;
                            path_found_for_current_spec = true;
                        }
                    }
                    for(const auto& pair : head_files_map) {
                       if (pair.first != user_spec_path && Utils::is_path_under_or_equal(pair.first, user_spec_path)) {
                            paths_to_process.insert(pair.first);
                            path_found_for_current_spec = true;
                       }
                    }
                } else {
                    std::cout << "警告: 无法规范化路径 '" << p_user_input.string() << "'，已跳过。" << std::endl;
                }
            }
            // 对于已在 paths_to_process 中的文件，如果它在索引中，确保 index_files_map 有其条目
            for(const auto& path_in_set : paths_to_process) {
                if(index_files_map.find(path_in_set) == index_files_map.end()) { // 如果还不在map里
                     auto idx_entry_it = std::find_if(index_entries.begin(), index_entries.end(),
                                                      [&](const IndexEntry& e){ return e.file_path == path_in_set;});
                     if(idx_entry_it != index_entries.end()) {
                         index_files_map[path_in_set] = idx_entry_it->blob_hash_hex;
                     }
                }
            }
        }

        // 遍历确定的路径进行比较
        for (const auto& path : paths_to_process) {
            auto it_idx = index_files_map.find(path);
            bool in_index = (it_idx != index_files_map.end());
            std::string index_blob_hash = in_index ? it_idx->second : "";

            auto it_head = head_files_map.find(path);
            bool in_head = (it_head != head_files_map.end());
            std::string head_blob_hash = in_head ? it_head->second.first : "";

            // 如果两边都存在且哈希相同，或者两边都不存在，则跳过
            if (index_blob_hash == head_blob_hash && in_index == in_head) continue;

            auto lines_head_opt = head_blob_hash.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(head_blob_hash);
            auto lines_index_opt = index_blob_hash.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(index_blob_hash);

            if(lines_head_opt && lines_index_opt) {
                 _perform_and_print_file_diff(path, *lines_head_opt, " (HEAD)", *lines_index_opt, " (Index)");
            }
        }

    } else {
        // --- 模式 1: Working Directory vs Index ---
        const auto& index_entries = index_manager_.get_all_entries(); // 获取所有索引条目

        for (const auto& entry : index_entries) { // 遍历索引中的每个文件
            std::filesystem::path relative_path = entry.file_path; // 获取文件的相对路径

            // 检查是否需要处理此路径 (如果用户指定了特定路径的话)
            bool process_this_path = options.paths_to_diff.empty(); // 如果没指定路径，则默认处理所有索引中的文件
            if (!process_this_path) { // 如果指定了路径，则检查当前文件是否在指定范围内
                for (const auto& p_user_input : options.paths_to_diff) {
                    std::optional<std::filesystem::path> rel_p_user_spec_opt = normalize_and_relativize_path(p_user_input);
                    if (rel_p_user_spec_opt) {
                        if (Utils::is_path_under_or_equal(relative_path, *rel_p_user_spec_opt)) { // 使用辅助函数
                            process_this_path = true;
                            break;
                        }
                    }
                }
            }
            if (!process_this_path) continue; // 如果不需要处理此路径，则跳过

            std::string hash_from_index = entry.blob_hash_hex; // 获取索引中的 blob 哈希
            std::optional<std::vector<std::string>> lines_from_index_opt = _get_blob_lines(hash_from_index);
            if (!lines_from_index_opt) { // 如果无法加载索引中的内容，记录错误并跳过
                std::cerr << "警告 (diff): 无法加载索引中文件 '" << relative_path.string() << "' 的内容。" << std::endl;
                continue;
            }

            // 获取工作目录中对应文件的行内容
            std::optional<std::vector<std::string>> lines_from_wd_opt = _get_workdir_lines(relative_path);

            if (!lines_from_wd_opt.has_value()) { // 文件在索引中，但在工作目录中不存在 (被删除)
                _perform_and_print_file_diff(relative_path, *lines_from_index_opt, " (Index)", {}, " (Working Directory)");
            } else { // 文件在索引和工作目录中都存在
                // 计算工作目录文件的实际内容哈希，以判断是否真的发生了更改
                std::vector<std::byte> wd_byte_content;
                std::filesystem::path abs_wd_path = work_tree_root_ / relative_path;
                std::ifstream wd_ifs_bytes(abs_wd_path, std::ios::binary);
                if(wd_ifs_bytes) {
                    wd_ifs_bytes.seekg(0, std::ios::end); std::streamsize size = wd_ifs_bytes.tellg();
                    wd_ifs_bytes.seekg(0, std::ios::beg);
                    if (size >= 0) { // 允许空文件
                        wd_byte_content.resize(static_cast<size_t>(size));
                        if (size > 0) { // 只有在文件非空时才读取
                            if(!wd_ifs_bytes.read(reinterpret_cast<char*>(wd_byte_content.data()), size)){
                                std::cerr << "警告 (diff): 读取工作目录文件 '" << abs_wd_path.string() << "' 内容失败。" << std::endl;
                                // 可以选择跳过，或者进行基于已读取内容的diff（如果部分读取）
                                // 为简单起见，如果读取失败，我们可能无法准确计算哈希，所以回退到直接内容比较
                                 _perform_and_print_file_diff(relative_path, *lines_from_index_opt, " (Index)", *lines_from_wd_opt, " (Working Directory)");
                                wd_ifs_bytes.close();
                                continue;
                            }
                        }
                    }  else { // 读取文件大小失败
                        std::cerr << "警告 (diff): 获取工作目录文件 '" << abs_wd_path.string() << "' 大小失败。" << std::endl;
                         _perform_and_print_file_diff(relative_path, *lines_from_index_opt, " (Index)", *lines_from_wd_opt, " (Working Directory)");
                        wd_ifs_bytes.close();
                        continue;
                    }
                } else { // 文件打开失败（理论上 _get_workdir_lines 已经检查过一次，但这里再次确保）
                     std::cerr << "警告 (diff): 无法打开工作目录文件 '" << abs_wd_path.string() << "' 以计算哈希。" << std::endl;
                     _perform_and_print_file_diff(relative_path, *lines_from_index_opt, " (Index)", *lines_from_wd_opt, " (Working Directory)");
                     continue;
                }
                wd_ifs_bytes.close();

                Blob wd_blob(wd_byte_content); //
                std::string hash_wd = SHA1::sha1(wd_blob.serialize()); //

                if (hash_wd != hash_from_index) { // 如果哈希不同，则内容已更改
                    _perform_and_print_file_diff(relative_path, *lines_from_index_opt, " (Index)", *lines_from_wd_opt, " (Working Directory)");
                }
            }
        }
    }
}



bool Repository::tag_create(const std::string& tag_name, const std::string& commit_ish_str) {
    // 1. 验证标签名 (简化版验证：非空，不含路径分隔符)
    if (tag_name.empty() || tag_name.find('/') != std::string::npos || tag_name.find('\\') != std::string::npos || tag_name == "." || tag_name == "..") {
        std::cerr << "错误: 无效的标签名称 '" << tag_name << "'。" << std::endl;
        return false;
    }
    // Git 对引用名称有更严格的规则，例如不能包含 ..，不能以 .lock 结尾等。

    // 2. 确定目标 commit 哈希
    std::optional<std::string> target_commit_hash_opt;
    if (commit_ish_str.empty()) {
        target_commit_hash_opt = _get_head_commit_hash(); //
        if (!target_commit_hash_opt) {
            std::cerr << "错误: HEAD 当前没有指向有效的 commit，无法创建标签。" << std::endl;
            return false;
        }
    } else {
        target_commit_hash_opt = _resolve_commit_ish_to_full_hash(commit_ish_str); //处理分支名、"HEAD"、短哈希和长哈希 尽可能完整commit哈
        if (!target_commit_hash_opt) {
            // _resolve_commit_ish_to_full_hash 内部会打印错误
            return false;
        }
    }
    std::string target_commit_hash = *target_commit_hash_opt;

    // 3. 检查标签是否已存在
    std::filesystem::path tag_file_path = get_tags_directory() / tag_name;
    std::error_code ec;
    if (std::filesystem::exists(tag_file_path, ec)) {
        std::cerr << "错误: 标签 '" << tag_name << "' 已存在。" << std::endl;
        return false;
    }
    if (ec) {
        std::cerr << "错误: 检查标签文件是否存在时出错: " << ec.message() << std::endl;
        return false;
    }

    // 4. 创建 refs/tags 目录 (如果不存在)
    std::filesystem::path tags_dir = get_tags_directory();
    if (!std::filesystem::exists(tags_dir, ec)) {
        if (!std::filesystem::create_directories(tags_dir, ec)) {
            std::cerr << "错误: 无法创建标签目录 '" << tags_dir.string() << "': " << ec.message() << std::endl;
            return false;
        }
    } else if (!std::filesystem::is_directory(tags_dir, ec)) {
        std::cerr << "错误: 路径 '" << tags_dir.string() << "' 已存在但不是一个目录。" << std::endl;
        return false;
    }
     if (ec) { // 检查 is_directory 或 exists 是否出错
        std::cerr << "错误: 访问标签目录时出错: " << ec.message() << std::endl;
        return false;
    }


    // 5. 创建并写入标签文件 (存储 commit 哈希)
    std::ofstream tag_ofs(tag_file_path);
    if (!tag_ofs.is_open()) {
        std::cerr << "错误: 无法创建标签文件 '" << tag_file_path.string() << "'。" << std::endl;
        return false;
    }
    tag_ofs << target_commit_hash << std::endl; // 写入 commit 哈希并换行
    tag_ofs.close();

    if (!tag_ofs.good()) { // 检查写入是否成功
        std::cerr << "错误: 写入标签文件 '" << tag_file_path.string() << "' 失败。" << std::endl;
        std::filesystem::remove(tag_file_path, ec); // 尝试删除写入失败的文件
        return false;
    }

    std::cout << "已创建轻量标签 '" << tag_name << "' 指向 " << target_commit_hash.substr(0, 7) << std::endl;
    return true;
}

void Repository::tag_list() const {
    std::filesystem::path tags_dir = get_tags_directory();
    std::error_code ec;

    if (!std::filesystem::exists(tags_dir, ec) || !std::filesystem::is_directory(tags_dir, ec)) {
        // 没有标签目录，或者它不是一个目录，就认为没有标签。
        // 不打印错误，因为这可能是正常情况（例如，仓库刚初始化，还没有标签）。
        return;
    }
     if (ec) {
        std::cerr << "错误: 访问标签目录时出错: " << ec.message() << std::endl;
        return;
    }


    std::set<std::string> tag_names; // 使用 set 来自动排序
    for (const auto& entry : std::filesystem::directory_iterator(tags_dir, ec)) {
        if (entry.is_regular_file(ec)) { // 标签应该是常规文件
            tag_names.insert(entry.path().filename().string());
        } else if (ec) {
            std::cerr << "警告 (tag_list): 检查路径 '" << entry.path().string() << "' 类型时出错: " << ec.message() << std::endl;
            // 可以选择继续或中止
        }
    }
    if (ec) { // directory_iterator 构造或迭代中的错误
        std::cerr << "错误 (tag_list): 遍历标签目录 '" << tags_dir.string() << "' 失败: " << ec.message() << std::endl;
        return;
    }

    for (const auto& tag_name : tag_names) {
        std::cout << tag_name << std::endl;
    }
}



bool Repository::tag_delete(const std::string& tag_name) {
    // 1. 验证标签名 (与创建时类似)
    if (tag_name.empty() || tag_name.find('/') != std::string::npos || tag_name.find('\\') != std::string::npos) {
        std::cerr << "错误: 无效的标签名称 '" << tag_name << "'。" << std::endl;
        return false;
    }

    std::filesystem::path tag_file_path = get_tags_directory() / tag_name;
    std::error_code ec;

    if (!std::filesystem::exists(tag_file_path, ec) || !std::filesystem::is_regular_file(tag_file_path, ec)) {
        std::cerr << "错误: 标签 '" << tag_name << "' 未找到。" << std::endl;
        return false;
    }
    if (ec) { // exists 或 is_regular_file 检查出错
        std::cerr << "错误: 检查标签文件 '" << tag_file_path.string() << "' 时出错: " << ec.message() << std::endl;
        return false;
    }

    if (std::filesystem::remove(tag_file_path, ec)) {
        std::cout << "已删除标签 '" << tag_name << "'" << std::endl;
        return true;
    } else {
        std::cerr << "错误: 删除标签文件 '" << tag_file_path.string() << "' 失败: " << ec.message() << std::endl;
        return false;
    }
}


/**
 * @brief 将指定分支的更改合并到当前活动分支。
 * @param branch_to_merge_name 要合并到当前分支的分支的名称、commit 哈希或其他可解析的 commit 标识符
 * @return 如果合并成功（包括快进或无冲突的三路合并并自动提交），返回 true。\n
 *         如果发生需要手动解决的冲突或错误，返回 false。
 * @details
 * 合并逻辑：
 *   1. 保证 工作区干净 、 OURS（HEAD）和 THEIRS（指定的）不指向同一个 commit
 *   2. 查找公共祖先   从 THEIRS 开始，向上遍历其所有父提交 遇到的第一个也存在于 OURS 祖先集合中的BASE
 *   3. BASE == THEIRS：THEIRS（要合并的分支）是 OURS（当前分支）的祖先 ， OURS 已经包含了 THEIRS 的所有更改，甚至可能更新， 合并结束
 *   4. BASE == OURS： OURS（当前分支）是 THEIRS（要合并的分支）的祖先且当前分支在分叉后没有产生新的提交 ，Fast-forward合并结束，不会创建新的合并提交
 *   5. 否则 三路合并 ，两个分支在共同祖先 `BASE` 之后各自独立发展，产生了不同的提交历史 \n
 *      A. 获取 `BASE`、`OURS`、`THEIRS` 三个 commit 指向的树对象 \n
 *      B. 比较并合并树内容  对于每个文件，根据其在三个版本中的状态（新增、删除、修改、内容是否冲突）决定合并策略 \n
 *      C. 处理冲突 （文件在 OURS 和 THEIRS 中都相对于 BASE 做了修改，并且修改的内容不同）如果检测到冲突，中止自动合并，提示用户手动解决 \n
 *      D. 创建合并提交 (如果无冲突或冲突已解决) 基于合并结果构建新的树对象 创建一个新的 commit 对象，它将有两个父提交 (`OURS` 和 `THEIRS`) 更新当前分支的引用指向这个新的合并提交 \n
 */
bool Repository::merge(const std::string& branch_to_merge_name) {
    std::error_code ec;
    std::filesystem::path merge_head_file_path = mygit_dir_ / MERGE_HEAD_FILE_NAME;

    // 检查是否已经处于合并状态
    if (std::filesystem::exists(merge_head_file_path)) {
        std::cerr << "错误: 您正处于一个合并过程中。请先解决冲突并提交 ('biogit commit')，或者中止合并。" << std::endl;
        return false;
    }

    // 0. 前提条件检查
    //  a. 保证工作区干净
    if (!is_workspace_clean()) { //
        std::cerr << "错误: 工作目录不干净。请在合并分支前提交或储藏您的更改。" << std::endl;
        return false;
    }

    //  b.获取当前commit
    std::optional<std::string> ours_commit_hash_opt = _get_head_commit_hash(); //
    if (!ours_commit_hash_opt) {
        std::cerr << "错误: 当前 HEAD 没有指向一个有效的 commit。无法开始合并。" << std::endl;
        return false;
    }
    std::string ours_commit_hash = *ours_commit_hash_opt;

    //  c.获取要合并的commit
    std::optional<std::string> theirs_commit_hash_opt = _resolve_commit_ish_to_full_hash(branch_to_merge_name); //
    if (!theirs_commit_hash_opt) {
        // _resolve_commit_ish_to_full_hash 内部会打印错误
        return false;
    }
    std::string theirs_commit_hash = *theirs_commit_hash_opt;

    //  d.保证两个commit不是同一个
    if (ours_commit_hash == theirs_commit_hash) {
        std::cout << "Already up to date." << std::endl;
        return true;
    }

    //  e.获取当前分支名，用于冲突标记和提交信息
    std::string current_branch_name_for_labels = "HEAD";
    std::filesystem::path head_file = get_head_file_path(); //
    std::string current_branch_ref_path_str;
    bool is_head_currently_on_branch = false; //判断当前HEAD 是指向分支还是分离

    if (std::filesystem::exists(head_file)) {
        std::ifstream h_ifs(head_file);
        std::string head_content_line;
        if (std::getline(h_ifs, head_content_line)) {
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) { head_content_line.pop_back(); }
            if (!head_content_line.empty() && head_content_line.back() == '\r') { head_content_line.pop_back(); }
            if (head_content_line.rfind("ref: refs/heads/", 0) == 0) {
                current_branch_ref_path_str = head_content_line.substr(std::string("ref: refs/heads/").length());
                current_branch_name_for_labels = current_branch_ref_path_str;
                is_head_currently_on_branch = true;
            }
        }
        h_ifs.close();
    }

    // 1. 查找共同祖先
    std::optional<std::string> base_commit_hash_opt = _find_common_ancestor(ours_commit_hash, theirs_commit_hash);
    if (!base_commit_hash_opt) {
        std::cerr << "错误: 无法找到分支 '" << branch_to_merge_name << "' 和当前 " << current_branch_name_for_labels << " 之间的共同历史。可能历史不相关。" << std::endl;
        return false;
    }
    std::string base_commit_hash = *base_commit_hash_opt;

    // 2. 处理各种合并场景
    //  a. BASE == THEIRS：THEIRS（要合并的分支）是 OURS（当前分支）的祖先 ，即OURS来自THEIRS，并且可能有新的修改，不合并返回
    if (base_commit_hash == theirs_commit_hash) {
        std::cout << "Already up to date." << std::endl;
        return true;
    }
    //  b. BASE == OURS： OURS（当前分支）是 THEIRS（要合并的分支）的祖先且当前分支在分叉后没有产生新的提交 ，OURS直接指向THEIRS，不创建新commit（Fast-forward合并）
    if (base_commit_hash == ours_commit_hash) {
        // 快进合并，保证需要指向一个分支
        if (!is_head_currently_on_branch) {
            std::cerr << "错误: HEAD 处于分离状态。要快进，请先检出到一个分支。" << std::endl;
            std::cerr << "或者，如果要将当前分离的 HEAD 更新到 '" << branch_to_merge_name << "' (它是一个后代)，请使用 'biogit switch " << branch_to_merge_name << "'。" << std::endl;
            return false;
        }
        std::cout << "正在快进合并..." << std::endl;
        std::cout << "Updating " << ours_commit_hash.substr(0, 7) << ".." << theirs_commit_hash.substr(0, 7) << std::endl;

        auto theirs_commit_obj = Commit::load_by_hash(theirs_commit_hash, get_objects_directory());
        if (!theirs_commit_obj) { std::cerr << "错误: 无法加载目标 commit '" << theirs_commit_hash.substr(0,7) << "' 进行快进。" << std::endl; return false; }

        std::map<std::filesystem::path, std::pair<std::string, std::string>> ours_files_map_ff;
        auto ours_commit_obj_ff = Commit::load_by_hash(ours_commit_hash, get_objects_directory());
        if(ours_commit_obj_ff) _load_tree_contents_recursive(ours_commit_obj_ff->tree_hash_hex, "", ours_files_map_ff);
        else { std::cerr << "错误: 无法加载当前 commit '" << ours_commit_hash.substr(0,7) << "' 以进行快进比较。" << std::endl; return false; }

        // 通过
        if (!_update_working_directory_from_tree(theirs_commit_obj->tree_hash_hex, ours_files_map_ff)) { std::cerr << "错误: 快进合并时更新工作目录失败。" << std::endl; return false; } //
        index_manager_.clear_in_memory(); //
        _populate_index_from_tree_recursive(theirs_commit_obj->tree_hash_hex, "", index_manager_); //
        if (!index_manager_.write()) { std::cerr << "严重错误: 快进合并时写入索引文件失败！" << std::endl; return false; } //

        std::filesystem::path branch_file_to_update = get_heads_directory() / current_branch_ref_path_str;
        std::ofstream ff_branch_ofs(branch_file_to_update, std::ios::trunc);
        if (!ff_branch_ofs) { std::cerr << "错误: 快进时无法更新分支文件 " << branch_file_to_update.string() << std::endl; return false;}
        ff_branch_ofs << theirs_commit_hash << std::endl;
        ff_branch_ofs.close();

        std::cout << "Fast-forward" << std::endl;
        std::cout << current_branch_name_for_labels << " 已更新为 " << theirs_commit_hash.substr(0,7) << std::endl;
        return true;
    }

    //  c. 三路合并 ，两个分支在共同祖先 `BASE` 之后各自独立发展，产生了不同的提交历史  需要检查每个文件是否冲突
    std::cout << "开始三路合并..." << std::endl;
    std::cout << "  共同祖先 (Base): " << base_commit_hash.substr(0, 7) << std::endl;
    std::cout << "  我们的版本 (Ours/" << current_branch_name_for_labels << "): " << ours_commit_hash.substr(0, 7) << std::endl;
    std::cout << "  他们的版本 (Theirs/" << branch_to_merge_name << "): " << theirs_commit_hash.substr(0, 7) << std::endl;

    //    c.1 读取三个commit对应的tree
    auto base_commit_obj = Commit::load_by_hash(base_commit_hash, get_objects_directory());
    auto ours_commit_obj = Commit::load_by_hash(ours_commit_hash, get_objects_directory());
    auto theirs_commit_obj = Commit::load_by_hash(theirs_commit_hash, get_objects_directory());

    if (!base_commit_obj || !ours_commit_obj || !theirs_commit_obj) {
        std::cerr << "错误: 无法加载合并所需的一个或多个 commit 对象。" << std::endl;
        return false;
    }

    //    c.2 递归加载 Tree 内容到 Map <相对路径, {Blob哈希, 模式}>
    std::map<std::filesystem::path, std::pair<std::string, std::string>> base_files, ours_files, theirs_files;
    _load_tree_contents_recursive(base_commit_obj->tree_hash_hex, "", base_files);
    _load_tree_contents_recursive(ours_commit_obj->tree_hash_hex, "", ours_files);
    _load_tree_contents_recursive(theirs_commit_obj->tree_hash_hex, "", theirs_files);

    //    c.3 不直接修改 index_manager_ ，通过临时变量 IndexEntry 列表用于最终的合并 commit  如果合并成功（无冲突），则用此结果更新 index_manager_
    std::vector<IndexEntry> merged_entries_accumulator; // 用于存储成功合并的条目
    std::vector<std::filesystem::path> conflict_paths_list; // 冲突的
    bool conflicts_occurred = false;

    //    c.4 保存所有路径
    std::set<std::filesystem::path> all_involved_paths;
    for(const auto& p : base_files) all_involved_paths.insert(p.first);
    for(const auto& p : ours_files) all_involved_paths.insert(p.first);
    for(const auto& p : theirs_files) all_involved_paths.insert(p.first);

    //    c.5 比较每个文件，判断是否冲突
    for (const auto& path : all_involved_paths) {
        auto base_it = base_files.find(path);
        auto ours_it = ours_files.find(path);
        auto theirs_it = theirs_files.find(path);

        std::string base_h = (base_it != base_files.end()) ? base_it->second.first : "";
        std::string ours_h = (ours_it != ours_files.end()) ? ours_it->second.first : "";
        std::string theirs_h = (theirs_it != theirs_files.end()) ? theirs_it->second.first : "";

        std::string result_mode = "100644";
        if(ours_it != ours_files.end()) result_mode = ours_it->second.second;
        else if (theirs_it != theirs_files.end()) result_mode = theirs_it->second.second;
        else if (base_it != base_files.end()) result_mode = base_it->second.second;

        std::string merged_blob_hash_for_index;
        bool current_file_had_conflict = false;

        if (ours_h == theirs_h) { // 没有冲突 当前分支（Ours）和要合并的分支（Theirs）中的文件内容完全一样
            merged_blob_hash_for_index = ours_h;
        } else if (base_h == ours_h && base_h != theirs_h) { // 文件内容从共同祖先（Base）到当前分支（Ours）没有发生变化，但是从共同祖先（Base）到要合并的分支（Theirs）发生了变化
            merged_blob_hash_for_index = theirs_h; // 采纳 Theirs 分支的修改
        } else if (base_h == theirs_h && base_h != ours_h) { // 文件内容从共同祖先（Base）到要合并的分支（Theirs）没有发生变化，但是从共同祖先（Base）到当前分支（Ours）发生了变化
            merged_blob_hash_for_index = ours_h; // 采纳 Ours 分支的修改
        } else {
            /**
             *冲突 包括
             * 1.两边都修改 且修改内容不同：即 base_h != ours_h 且 base_h != theirs_h 且 ours_h != theirs_h
             * 2.一边修改了文件, 另一边删除了文件
             * 3.两边都添加了同名文件，但内容不同
             ***/
            conflicts_occurred = true; // 标记整个合并过程遇到了冲突
            current_file_had_conflict = true; // 标记当前这个文件发生了冲突
            conflict_paths_list.push_back(path); // 将冲突文件的路径记录下来

            std::cout << "冲突: 文件 " << path.string() << " 在两边都有不兼容的更改。" << std::endl;
            auto ours_lines_opt = ours_h.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(ours_h); //
            auto theirs_lines_opt = theirs_h.empty() ? std::make_optional<std::vector<std::string>>({}) : _get_blob_lines(theirs_h); //

            if (!_write_conflict_markers(path, current_branch_name_for_labels, ours_lines_opt, branch_to_merge_name, theirs_lines_opt)) {
                // 写入冲突标记失败是一个问题，但合并流程应继续以报告所有冲突
            }
        }

        // 只有无冲突且合并结果非空（即文件未被两边都删除或只在一边删除）时，才加入合并条目列表
        if (!current_file_had_conflict && !merged_blob_hash_for_index.empty()) {
            // 元数据获取简化：使用当前时间，大小从 blob 获取
            std::chrono::system_clock::time_point mtime = std::chrono::system_clock::now();
            uint64_t fsize = 0;
            auto merged_blob = Blob::load_by_hash(merged_blob_hash_for_index, get_objects_directory());
            if (merged_blob) {
                fsize = merged_blob->content.size();
                merged_entries_accumulator.push_back({result_mode, merged_blob_hash_for_index, mtime, fsize, path});
            } else {
                std::cerr << "错误: 无法加载合并产生的 blob '" << merged_blob_hash_for_index << "' 用于文件 " << path.string() << std::endl;
                conflicts_occurred = true; // 视为一个严重错误，转入冲突流程
                conflict_paths_list.push_back(path); // 标记此文件也发生了问题
            }
        }
    }

    // 3. 发生冲突，打印冲突的文件（重点，不更新index 用户解决冲突后需要add commit）
    if (conflicts_occurred) {
        std::cerr << "自动合并失败。请修复以下文件中的冲突:" << std::endl;
        for (const auto& p : conflict_paths_list) {
            std::cerr << "  " << p.string() << std::endl;
        }
        std::cerr << "请先手动解决冲突，并使用 'add <文件>...' 刷新暂存区，然后运行commit再次合并" << std::endl;

        std::ofstream file_conflicts(mygit_dir_ /FILE_CONFLICTS);
        if (!file_conflicts) { std::cerr << "严重错误: 无法创建 MERGE_HEAD 文件。" << std::endl; return false; }
        else {
            for (const auto& p : conflict_paths_list) {
                file_conflicts << p.string() << std::endl;
            }
            file_conflicts.close();
        }

        std::ofstream merge_head_ofs(merge_head_file_path);
        if (!merge_head_ofs) { std::cerr << "严重错误: 无法创建 MERGE_HEAD 文件。" << std::endl; return false; }
        merge_head_ofs << theirs_commit_hash << std::endl;
        merge_head_ofs.close();

        return false;
    }


    // 4. 如果没有冲突，进行合并
    std::cout << "自动合并完成，所有文件均无冲突。" << std::endl;

    // 将成功合并的条目更新到主 index_manager_
    //  4.1 刷新暂存区 ，加入所有合并文件
    index_manager_.clear_in_memory(); //
    for(const auto& merged_entry_data : merged_entries_accumulator) {
        index_manager_.add_or_update_entry(
            merged_entry_data.file_path,
            merged_entry_data.blob_hash_hex,
            merged_entry_data.mode,
            merged_entry_data.mtime, // 注意：这里的mtime是合并时的时间
            merged_entry_data.file_size
        );
    }
    if (!index_manager_.write()) { /* ... 错误处理 ... */ return false; } //重要错误 无法修改

    //  4.2 刷新暂存区 ，加入所有合并文件
    std::string merged_final_tree_hash;
    if (!index_manager_.get_all_entries().empty()) {
        auto merged_tree_hash_opt = _build_trees_and_get_root_hash(index_manager_.get_all_entries()); //
        if (!merged_tree_hash_opt) { std::cerr<<"错误: 构建合并树失败"<<std::endl; return false; }
        merged_final_tree_hash = *merged_tree_hash_opt;
    } else {
        Tree empty_tree; auto et_hash_opt = empty_tree.save(get_objects_directory());
        if (!et_hash_opt) { std::cerr<<"错误: 保存空合并树失败"<<std::endl; return false; }
        merged_final_tree_hash = *et_hash_opt;
    }

    //  4.3 更新工作目录  这个时候已经没用冲突， 随意选择一个文件列表就行
    if (!_update_working_directory_from_tree(merged_final_tree_hash, ours_files)) { //
        std::cerr << "错误: 合并后更新工作目录失败。" << std::endl;
        return false;
    }

    //  4.4 创建新commit得各种信息
    std::string merge_commit_message = "Merge branch '" + branch_to_merge_name + "'";
    if (is_head_currently_on_branch) {
        merge_commit_message += " into " + current_branch_name_for_labels;
    }

    PersonTimestamp author_info, committer_info; // (与 commit 方法中获取作者/提交者信息的逻辑相同)
    std::string user_name = "Default User"; std::string user_email = "user@example.com";
    auto current_time_point = std::chrono::system_clock::now();
    std::time_t current_time_t = std::chrono::system_clock::to_time_t(current_time_point);
    std::tm local_tm = *std::localtime(&current_time_t); char tz_buffer[6];
    if (std::strftime(tz_buffer, sizeof(tz_buffer), "%z", &local_tm)) { author_info.timezone_offset = tz_buffer; }
    else { author_info.timezone_offset = "+0000"; }
    author_info.name = user_name; author_info.email = user_email; author_info.timestamp = current_time_point;
    committer_info = author_info;

    Commit new_merge_commit;
    new_merge_commit.tree_hash_hex = merged_final_tree_hash;
    new_merge_commit.parent_hashes_hex = {ours_commit_hash, theirs_commit_hash};
    new_merge_commit.author = author_info;
    new_merge_commit.committer = committer_info;
    new_merge_commit.message = merge_commit_message;

    auto new_merge_commit_hash_opt = new_merge_commit.save(get_objects_directory()); //
    if (!new_merge_commit_hash_opt) { std::cerr << "错误: 保存合并提交失败。" <<std::endl; return false; }
    std::string new_merge_commit_hash = *new_merge_commit_hash_opt;

    std::cout << "Merge made by 'simple' strategy." << std::endl;
    std::cout << "Committed merge " << new_merge_commit_hash.substr(0,7) << std::endl;

    //  4.5 更新当前分支的引用或直接更新 HEAD
    if (is_head_currently_on_branch && !current_branch_ref_path_str.empty()) {
        std::filesystem::path branch_file_to_update = get_heads_directory() / current_branch_ref_path_str;
        std::ofstream m_branch_ofs(branch_file_to_update, std::ios::trunc);
        if (!m_branch_ofs) { std::cerr << "错误: 更新分支文件失败 " << branch_file_to_update.string() << std::endl; return false;}
        m_branch_ofs << new_merge_commit_hash << std::endl;
        m_branch_ofs.close();
    } else {
        std::ofstream direct_head_ofs(head_file, std::ios::trunc);
        if (!direct_head_ofs) { std::cerr << "错误: 更新 HEAD 文件失败。" << std::endl; return false; }
        direct_head_ofs << new_merge_commit_hash << std::endl;
        direct_head_ofs.close();
        std::cout << "HEAD (detached) updated to merge commit " << new_merge_commit_hash.substr(0,7) << std::endl;
    }
    return true;
}


/**
 * @brief 添加一个新的远程仓库配置。
 * @param name 远程仓库的别名 (例如 "origin")。
 * @param url 远程仓库的 URL (例如 "localhost:8080/path/to/repo")。
 * @return 如果成功添加或更新，返回 true；否则返回 false。
 */
bool Repository::remote_add(const std::string& name, const std::string& url) {
    if (name.empty() || url.empty()) {
        std::cerr << "错误: 远程名称和URL不能为空。" << std::endl;
        return false;
    }
    if (name.find_first_of(" \t\n\r\"") != std::string::npos) {
        std::cerr << "错误: 远程名称 '" << name << "' 包含无效字符。" << std::endl;
        return false;
    }

    std::string url_key = "remote." + name + ".url";
    std::string fetch_key = "remote." + name + ".fetch";
    std::string default_fetch_refspec = "+refs/heads/*:refs/remotes/" + name + "/*";

    // 简化为连续调用 config_set
    bool url_set = config_set(url_key, url);
    bool fetch_set = config_set(fetch_key, default_fetch_refspec);

    if (url_set && fetch_set) {
        std::cout << "已添加/更新远程 '" << name << "' -> " << url << std::endl;
        std::cout << "  Fetch refspec: " << default_fetch_refspec << std::endl;
        return true;
    } else {
        std::cerr << "错误: 设置远程 '" << name << "' 配置失败。" << std::endl;
        // 尝试回滚 (如果可能且需要)
        if (!url_set) config_set(url_key, ""); // 尝试清除
        if (!fetch_set) config_set(fetch_key, ""); // 尝试清除
        return false;
    }
}


/**
 * @brief 移除一个已配置的远程仓库。
 * @param name 要移除的远程仓库的别名。
 * @return 如果成功移除，返回 true；如果未找到该远程仓库，返回 false。
 */
bool Repository::remote_remove(const std::string& name) {
    if (name.empty()) {
        std::cerr << "错误: 远程名称不能为空。" << std::endl;
        return false;
    }
    std::map<std::string, std::string> configs = load_all_config();
    std::string prefix_to_remove = "remote." + name + ".";
    bool removed_any = false;
    for (auto it = configs.begin(); it != configs.end(); ) {
        if (it->first.rfind(prefix_to_remove, 0) == 0) {
            it = configs.erase(it);
            removed_any = true;
        } else {
            ++it;
        }
    }

    if (removed_any) {
        if(save_all_config(configs)) {
            std::cout << "已移除远程 '" << name << "' 的所有配置。" << std::endl;
            return true;
        } else {
            std::cerr << "错误: 保存配置失败，远程 '" << name << "' 的配置可能未完全从文件中移除。" << std::endl;
            return false;
        }
    } else {
        std::cerr << "错误: 远程 '" << name << "' 未找到或没有配置项。" << std::endl;
        return false;
    }
}


/**
 * @brief 获取指定远程仓库的配置信息 (URL 和 fetch refspec)。
 * @param name 远程仓库的别名。
 * @return 如果找到，返回 RemoteConfig 结构体；否则返回 std::nullopt。
 */
std::optional<RemoteConfig> Repository::remote_get_config(const std::string& name) const {
    RemoteConfig rc;
    bool url_found = false;
    bool fetch_found = false;

    auto url_opt = config_get("remote." + name + ".url");
    if (url_opt) {
        rc.url = *url_opt;
        url_found = true;
    }
    auto fetch_opt = config_get("remote." + name + ".fetch");
    if (fetch_opt) {
        rc.fetch_refspec = *fetch_opt;
        fetch_found = true;
    }

    if (url_found) { // 至少 URL 必须存在
        return rc;
    }
    return std::nullopt;
}


/**
 * @brief 列出所有已配置的远程仓库及其主要配置 (例如 URL)。
 * @return 一个包含 <远程名称, RemoteConfig> 对的 map。
 */
std::map<std::string, RemoteConfig> Repository::remote_list_configs() const {
    std::map<std::string, RemoteConfig> remotes;
    std::map<std::string, std::string> all_configs = load_all_config();

    std::map<std::string, RemoteConfig> temp_remotes_map;

    for (const auto& pair : all_configs) {
        const std::string& flat_key = pair.first;
        const std::string& value = pair.second;

        if (flat_key.rfind("remote.", 0) == 0) {
            size_t first_dot = std::string("remote.").length();
            size_t second_dot = flat_key.find('.', first_dot);
            if (second_dot != std::string::npos) {
                std::string remote_name = flat_key.substr(first_dot, second_dot - first_dot);
                std::string key_in_section = flat_key.substr(second_dot + 1);
                if (key_in_section == "url") {
                    temp_remotes_map[remote_name].url = value;
                } else if (key_in_section == "fetch") {
                    temp_remotes_map[remote_name].fetch_refspec = value;
                }
            }
        }
    }
    // 筛选掉没有 URL 的远程配置 (如果可能存在这种情况)
    for (const auto& pair : temp_remotes_map) {
        if (!pair.second.url.empty()) {
            remotes[pair.first] = pair.second;
        }
    }
    return remotes;
}


/**
 * @brief 设置一个配置项的值。
 * @param key 配置项的键 (例如 "user.name", "remote.origin.url")。
 * @param value 配置项的值。
 * @return 如果成功设置，返回 true。
 */
bool Repository::config_set(const std::string& key, const std::string& value) {
    if (key.find('.') == std::string::npos || key.front() == '.' || key.back() == '.') {
        std::cerr << "错误: 无效的配置键格式 '" << key << "'。应为 'section.key' 或 'section.subsection.key'。" << std::endl;
        return false;
    }
    std::map<std::string, std::string> configs = load_all_config();
    configs[key] = value;
    return save_all_config(configs);
}


/**
 * @brief 获取一个配置项的值。
 * @param key 配置项的键。
 * @return 如果找到，返回配置值；否则返回 std::nullopt。
 */
std::optional<std::string> Repository::config_get(const std::string& key) const {
    std::map<std::string, std::string> configs = load_all_config();
    auto it = configs.find(key);
    if (it != configs.end()) {
        return it->second;
    }
    return std::nullopt;
}


/**
 * @brief 获取所有配置项。
 * @return 包含所有配置的 map。
 */
std::map<std::string, std::string> Repository::config_get_all() const {
    return load_all_config();
}

std::optional<UserConfig> Repository::get_user_config() const {
    UserConfig user_cfg;
    bool name_found = false;
    bool email_found = false;

    auto name_opt = config_get("user.name");
    if (name_opt) {
        user_cfg.name = *name_opt;
        name_found = true;
    }
    auto email_opt = config_get("user.email");
    if (email_opt) {
        user_cfg.email = *email_opt;
        email_found = true;
    }

    if (name_found || email_found) { // 只要找到一个就返回
        return user_cfg;
    }
    return std::nullopt;
}


/**
 * @brief 获取仓库中所有本地引用（分支和标签）及其指向的commit哈希。
 * @return 一个包含 <引用全名 (例如 "refs/heads/main"), commit哈希> 对的vector。
 */
std::vector<std::pair<std::string, std::string>> Repository::get_all_local_refs() const {
    std::vector<std::pair<std::string, std::string>> refs_info;
    std::error_code ec;
    // 0. 获取 HEAD 引用
    std::filesystem::path head_file_path = get_head_file_path(); //
    if (std::filesystem::exists(head_file_path)) {
        std::ifstream head_ifs(head_file_path);
        std::string head_content_line;
        if (std::getline(head_ifs, head_content_line)) {
            // 清理行尾换行符
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) head_content_line.pop_back();
            if (!head_content_line.empty() && head_content_line.back() == '\r') head_content_line.pop_back();

            refs_info.push_back({"HEAD", head_content_line}); // <-- 已经添加了 HEAD
        }
    }

    // 1. 获取所有本地分支 (heads)
    std::filesystem::path heads_dir = get_heads_directory(); //
    if (std::filesystem::exists(heads_dir, ec) && std::filesystem::is_directory(heads_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(heads_dir, ec)) {
            // 只处理常规文件，忽略可能的子目录（虽然refs/heads下通常不应该有子目录）
            if (entry.is_regular_file(ec)) {
                std::filesystem::path ref_file_path = entry.path();
                std::ifstream ref_file(ref_file_path);
                std::string commit_hash;
                if (ref_file >> commit_hash && commit_hash.length() == 40) {
                    // 构造完整的引用名称，例如 "refs/heads/main"
                    std::string ref_full_name = "refs/heads/" + ref_file_path.filename().string();
                    refs_info.push_back({ref_full_name, commit_hash});
                } else {
                    std::cerr << "警告: 无法读取或解析引用文件: " << ref_file_path.string() << std::endl;
                }
            }
        }
        if (ec) { // 迭代器本身的错误
            std::cerr << "警告: 遍历heads目录时出错: " << ec.message() << std::endl;
        }
    } else if (ec) { // exists 或 is_directory 的错误
        std::cerr << "警告: 访问heads目录时出错: " << ec.message() << std::endl;
    }


    // 2. 获取所有本地标签 (tags)
    std::filesystem::path tags_dir = get_tags_directory(); //
    if (std::filesystem::exists(tags_dir, ec) && std::filesystem::is_directory(tags_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(tags_dir, ec)) {
            if (entry.is_regular_file(ec)) {
                std::filesystem::path tag_file_path = entry.path();
                std::ifstream tag_file(tag_file_path);
                std::string object_hash; // 标签可能指向commit，也可能指向tag对象（附注标签）
                                        // 对于轻量标签，这里直接是commit哈希
                if (tag_file >> object_hash && object_hash.length() == 40) {
                    // 构造完整的标签名称，例如 "refs/tags/v1.0"
                    std::string tag_full_name = "refs/tags/" + tag_file_path.filename().string();

                    // 注意：轻量标签直接指向commit。如果是附注标签，这里读到的是tag对象的哈希，
                    // 还需要进一步解析tag对象来获取它最终指向的commit哈希。
                    // 为了简化，我们当前假设所有标签文件直接存储commit哈希（即只支持轻量标签的简单读取）。
                    // 如果要支持解析附注标签，需要加载tag对象并获取其 'object' 字段。
                    refs_info.push_back({tag_full_name, object_hash});
                } else {
                    std::cerr << "警告: 无法读取或解析标签文件: " << tag_file_path.string() << std::endl;
                }
            }
        }
        if (ec) {
            std::cerr << "警告: 遍历tags目录时出错: " << ec.message() << std::endl;
        }
    } else if (ec) {
        std::cerr << "警告: 访问tags目录时出错: " << ec.message() << std::endl;
    }

    // for (auto i:refs_info) {
    //     cout<<i.first<<"   "<<i.second<<endl;
    // }
    return refs_info;
}


/**
 * @brief 根据对象哈希（可以是完整哈希或明确的前缀）读取对象的完整原始文件内容。
 * @param object_hash 对象的SHA1哈希或唯一前缀。
 * @return 如果找到对象，返回包含其完整原始内容（包括头部 "type size\0"）的std::vector<char>；
 * 否则返回std::nullopt。
 */
std::optional<std::vector<char>> Repository::get_raw_object_content(const std::string& object_hash) const {
    // 使用已有的 _find_object_file_by_prefix 来定位对象文件
    std::optional<std::filesystem::path> object_file_path_opt = _find_object_file_by_prefix(object_hash); //

    if (!object_file_path_opt) {
        // std::cerr << "调试: 对象 " << object_hash << " 未找到或有歧义。" << std::endl;
        return std::nullopt; // 对象未找到或哈希前缀有歧义
    }

    std::filesystem::path object_file_path = *object_file_path_opt;
    std::ifstream object_file(object_file_path, std::ios::binary | std::ios::ate); // ate 打开并定位到末尾以获取大小

    if (!object_file.is_open()) {
        std::cerr << "错误: 无法打开对象文件进行读取: " << object_file_path.string() << std::endl;
        return std::nullopt;
    }

    std::streamsize size = object_file.tellg(); // 获取文件大小
    object_file.seekg(0, std::ios::beg);    // 重置回文件开头

    if (size < 0) { // tellg 可能返回 -1 如果出错
        std::cerr << "错误: 获取对象文件大小失败: " << object_file_path.string() << std::endl;
        return std::nullopt;
    }

    std::vector<char> buffer(static_cast<size_t>(size));
    if (size > 0) { // 只有当文件非空时才读取
        if (!object_file.read(buffer.data(), size)) {
            std::cerr << "错误: 读取对象文件内容失败: " << object_file_path.string() << std::endl;
            return std::nullopt;
        }
    }
    // 对于空对象（理论上不应该有，但以防万一），buffer 会是空的，这是正确的。

    return buffer; // 返回包含对象完整原始内容的字节向量
}


/**
 * @brief 检查具有给定哈希（或唯一前缀）的对象是否存在于仓库中。
 * @param object_hash 对象的SHA1哈希或能唯一识别对象的前缀。
 * @return 如果对象存在则返回 true，否则返回 false。
 */
bool Repository::object_exists(const std::string& object_hash) const {
    // _find_object_file_by_prefix 会查找对象文件，如果找到并唯一则返回路径
    // 如果返回 std::nullopt，则表示对象不存在或哈希有歧义（对于存在性检查，歧义也意味着“未明确存在”）
    return _find_object_file_by_prefix(object_hash).has_value(); //
}

/**
 * @brief 将对象的原始序列化数据直接写入对象库。
 * @param object_hash 对象的正确SHA1哈希 (应由调用者预先计算或验证)。
 * @param raw_data 指向对象完整原始内容数据 (包括 "type size\0" 头部) 的指针。
 * @param length raw_data 的长度。
 * @return 如果写入成功或对象已存在，返回 true；否则返回 false。
 */
bool Repository::write_raw_object(const std::string& object_hash, const char* raw_data, uint32_t length) {
    if (object_hash.length() != 40) {
        std::cerr << "Repository Error (write_raw_object): Invalid hash length for '" << object_hash << "'." << std::endl;
        return false;
    }
    // 注意：这里的 length 是原始 Git 对象（带头部）的长度。
    // 一个有效的 Git 对象序列化后，其长度通常不会是0，除非是极其特殊或错误的情况。
    // 例如，一个空文件对应的 blob 对象是 "blob 0\0"，长度为7。
    // 如果 length 为 0，但 object_hash 不是空字符串的哈希，则数据可能不完整。
    // 不过，由于哈希已经在 LogicSystem 中校验过了，这里可以信任 object_hash 和 length 的匹配性。

    std::filesystem::path objects_dir = get_objects_directory(); // 获取 .biogit/objects/ 路径
    std::filesystem::path subdir = objects_dir / object_hash.substr(0, 2); // 哈希前两位作为子目录
    std::filesystem::path final_obj_path = subdir / object_hash.substr(2); // 后38位作为文件名
    std::error_code ec;

    // 1. 检查对象是否已存在
    if (std::filesystem::exists(final_obj_path, ec)) {
        // std::cout << "Repository Info (write_raw_object): Object " << object_hash << " already exists. No need to write." << std::endl;
        return true; // 对象已存在，视为成功
    }
    if (ec) { // exists 检查本身出错
        std::cerr << "Repository Error (write_raw_object): Error checking existence of " << final_obj_path.string() << ": " << ec.message() << std::endl;
        return false;
    }

    // 2. 创建子目录 (如果不存在)
    if (!std::filesystem::exists(subdir, ec)) {
        if (!std::filesystem::create_directories(subdir, ec)) {
            std::cerr << "Repository Error (write_raw_object): Failed to create object subdir: " << subdir.string() << " - " << ec.message() << std::endl;
            return false;
        }
    } else if (!std::filesystem::is_directory(subdir, ec)) { // 如果路径存在但不是目录
        std::cerr << "Repository Error (write_raw_object): Object subdir path exists but is not a directory: " << subdir.string() << std::endl;
        return false;
    }
    if (ec) { // is_directory 或 create_directories 的其他错误
        std::cerr << "Repository Error (write_raw_object): Filesystem error with subdir " << subdir.string() << ": " << ec.message() << std::endl;
        return false;
    }

    // 3. 写入对象文件
    std::ofstream ofs(final_obj_path, std::ios::binary | std::ios::trunc); // 以二进制和覆盖模式打开
    if (!ofs.is_open()) {
        std::cerr << "Repository Error (write_raw_object): Failed to open object file for writing: " << final_obj_path.string() << std::endl;
        return false;
    }
    if (length > 0) { // 只有当有数据时才写入
        ofs.write(raw_data, length);
    }
    ofs.close();

    if (!ofs.good()) { // 检查写入和关闭操作是否都成功
        std::cerr << "Repository Error (write_raw_object): Failed to write or close object file: " << final_obj_path.string() << std::endl;
        std::filesystem::remove(final_obj_path, ec); // 尝试删除写入失败或不完整的文件
        return false;
    }
    // std::cout << "Repository Info (write_raw_object): Successfully wrote object " << object_hash << std::endl;
    return true;
}


/**
 * @brief 更新或创建指定的引用，使其指向新的 commit 哈希。
 * @param ref_full_name 要更新的引用的完整名称 (例如 "refs/heads/main", "refs/tags/v1.0")。
 * @param new_commit_hash 新的 commit SHA-1 哈希。
 * @param expected_old_commit_hash (可选) 客户端期望该引用当前指向的旧 commit 哈希。 如果提供此参数，服务器将进行检查。如果为 std::nullopt，则通常表示创建新引用或强制更新。
 * @param allow_non_fast_forward (可选) 是否允许非快进更新 (类似于 git push --force)。默认为false。
 * @return 更新操作的结果。
 */
Repository::UpdateRefResult Repository::update_ref(
    const std::string& ref_full_name,
    const std::string& new_commit_hash,
    const std::optional<std::string>& expected_old_commit_hash,
    bool allow_non_fast_forward) {

    // 1. 参数校验：引用名称格式
    if (!(ref_full_name.rfind("refs/heads/", 0) == 0 || ref_full_name.rfind("refs/tags/", 0) == 0) ||
        ref_full_name.back() == '/' || ref_full_name.find("..") != std::string::npos ||
        ref_full_name.find("//") != std::string::npos) {
        std::cerr << "Repository Error (update_ref): Invalid ref name format: " << ref_full_name << std::endl;
        return UpdateRefResult::INVALID_REF_NAME;
    }

    // 2. 参数校验：new_commit_hash 格式和存在性
    if (new_commit_hash.length() != 40 || !std::all_of(new_commit_hash.begin(), new_commit_hash.end(), ::isxdigit)) {
        std::cerr << "Repository Error (update_ref): Invalid new_commit_hash format: " << new_commit_hash << std::endl;
        return UpdateRefResult::NEW_COMMIT_NOT_FOUND; // 或者更具体的错误码
    }
    // 检查 new_commit_hash 指向的 commit 对象是否存在且确实是 commit 类型
    auto commit_obj_opt = Commit::load_by_hash(new_commit_hash, get_objects_directory()); //
    if (!commit_obj_opt) {
        std::cerr << "Repository Error (update_ref): New commit object " << new_commit_hash << " not found in repository." << std::endl;
        return UpdateRefResult::NEW_COMMIT_NOT_FOUND;
    }

    // 3. 获取引用文件的路径
    std::filesystem::path ref_file_path = mygit_dir_ / ref_full_name;
    std::error_code ec;

    std::string current_ref_value_on_server;
    bool ref_existed_on_server = false;

    if (std::filesystem::exists(ref_file_path, ec)) {
        if (!std::filesystem::is_regular_file(ref_file_path, ec)) {
            std::cerr << "Repository Error (update_ref): Ref path " << ref_file_path.string() << " exists but is not a file." << std::endl;
            return UpdateRefResult::IO_ERROR;
        }
        std::ifstream ifs(ref_file_path);
        if (ifs >> current_ref_value_on_server && current_ref_value_on_server.length() == 40) {
            ref_existed_on_server = true;
        } else {
            // 文件存在但内容无效或为空，视为不存在或需要被覆盖
            current_ref_value_on_server.clear(); // 清空，以便后续创建逻辑
        }
        ifs.close();
    } else if (ec) { // exists 检查出错
         std::cerr << "Repository Error (update_ref): Error checking existence of ref file " << ref_file_path.string() << ": " << ec.message() << std::endl;
         return UpdateRefResult::IO_ERROR;
    }
    // 如果文件不存在，ref_existed_on_server 为 false, current_ref_value_on_server 为空

    // 4. 处理 expected_old_commit_hash (如果客户端提供了)
    if (expected_old_commit_hash.has_value()) {
        if (!ref_existed_on_server) {
            // 客户端期望旧引用存在，但服务器上不存在
            std::cerr << "Repository Info (update_ref): Client expected ref " << ref_full_name
                      << " to exist (pointing to " << *expected_old_commit_hash << "), but it was not found on server." << std::endl;
            return UpdateRefResult::REF_NOT_FOUND_FOR_UPDATE; // 或者 OLD_HASH_MISMATCH
        }
        if (current_ref_value_on_server != *expected_old_commit_hash) {
            std::cerr << "Repository Info (update_ref): Old hash mismatch for ref " << ref_full_name
                      << ". Client expected: " << *expected_old_commit_hash
                      << ", Server has: " << current_ref_value_on_server << std::endl;
            return UpdateRefResult::OLD_HASH_MISMATCH;
        }
    }
    // 如果客户端没有提供 expected_old_commit_hash，我们通常允许创建新引用或直接覆盖旧引用（除非有其他策略如fast-forward）

    // 5. Fast-Forward 检查 (主要针对分支 refs/heads/*)
    bool is_branch_update = (ref_full_name.rfind("refs/heads/", 0) == 0);
    if (is_branch_update && ref_existed_on_server && !allow_non_fast_forward) {
        // 只有当分支已存在，且不允许非快进时，才进行检查
        // 如果 current_ref_value_on_server 为空（例如，旧引用文件无效被清空），则也视为创建新分支
        if (!current_ref_value_on_server.empty() && !is_fast_forward(current_ref_value_on_server, new_commit_hash)) {
            std::cerr << "Repository Info (update_ref): Update to ref " << ref_full_name
                      << " (from " << current_ref_value_on_server.substr(0,7)
                      << " to " << new_commit_hash.substr(0,7) << ") is not a fast-forward." << std::endl;
            return UpdateRefResult::NOT_FAST_FORWARD;
        }
    }

    // 6. 写入新的引用值
    // 确保父目录存在 (例如 refs/heads/feature/)
    std::filesystem::path parent_dir = ref_file_path.parent_path();
    if (!std::filesystem::exists(parent_dir, ec)) {
        if (!std::filesystem::create_directories(parent_dir, ec)) {
            std::cerr << "Repository Error (update_ref): Failed to create parent directory " << parent_dir.string() << ": " << ec.message() << std::endl;
            return UpdateRefResult::IO_ERROR;
        }
    } else if (!std::filesystem::is_directory(parent_dir, ec)) {
        std::cerr << "Repository Error (update_ref): Parent path " << parent_dir.string() << " is not a directory." << std::endl;
        return UpdateRefResult::IO_ERROR;
    }
    if (ec) {
        std::cerr << "Repository Error (update_ref): Filesystem error with parent directory " << parent_dir.string() << ": " << ec.message() << std::endl;
        return UpdateRefResult::IO_ERROR;
    }


    std::ofstream ofs(ref_file_path, std::ios::trunc); // 覆盖写入
    if (!ofs.is_open()) {
        std::cerr << "Repository Error (update_ref): Failed to open ref file for writing: " << ref_file_path.string() << std::endl;
        return UpdateRefResult::IO_ERROR;
    }
    ofs << new_commit_hash << std::endl; // Git 引用文件通常以换行结束
    ofs.close();

    if (!ofs.good()) {
        std::cerr << "Repository Error (update_ref): Failed to write or close ref file: " << ref_file_path.string() << std::endl;
        return UpdateRefResult::IO_ERROR;
    }

    std::cout << "Repository Info (update_ref): Successfully updated ref '" << ref_full_name
              << "' to point to '" << new_commit_hash.substr(0,7) << "'" << std::endl;
    return UpdateRefResult::SUCCESS;
}


/**
 * @brief 将本地分支的更改推送到指定的远程仓库。
 * @param remote_name 要推送到的远程仓库的别名 (例如 "origin")。
 * @param local_ref_full_name_param 要推送的本地引用的名称 (例如 "main" 或 "refs/heads/main")。
 * @param remote_ref_full_name_on_server_param 要在远程仓库上更新或创建的引用的名称 (例如 "main" 或 "refs/heads/main")。
 * @param force 是否执行强制推送 (如果为true，则允许非fast-forward更新)。
 * @param token 用于向服务器进行身份验证的认证 Token。
 * @return 如果推送成功，返回 true；否则返回 false。
 * @details
 * git push origin main： 将本地 main 分支的更改推送到名为 origin 的远程仓库的 main 分支
 * 需要检查分支的历史兼容性
 * 1. 远程仓库的 main 分支是你本地 main 分支的直接祖先
 *    A 本地 main: A -> B -> C
 *    B 远程 origin/main: A -> B
 *    C 远程仓库 origin 会简单地将其 main 分支的指针从 B 移动到 C
 * 2. 本地 main 分支与远程 origin/main 分支指向同一个 Commit
 *    A 不需要处理
 * 3. 本地 main 分支落后于远程 origin/main 分支 (Non-fast-forward, 本地需要先 pull/fetch)
 *    A 本地 main: A -> B
 *    B 远程 origin/main: A -> B -> C
 *    C 拒绝推送 ，直接推送会覆盖远程的 C 提交
 * 4. 本地 main 分支与远程 origin/main 分支历史分叉
 *    A 共同祖先: A
 *    B 本地 main: A -> B'
 *    C 远程 origin/main: A -> C'
 *    D 直接推送会尝试用你的 B' 历史覆盖远程的 C' 历史
 * 5. 强制推送
 *    A.无条件地将远程 main 分支的指针移动到你本地 main 指向的 commit
 * 6. 推送到一个远程不存在的分支
 *    A.本地 main: A -> B
 *    B 远程 origin: 没有名为 main 的分支
 *    C 创建一个新的远程分支 使其指向你本地 main 的最新 commit (B)
 */
bool Repository::push(const std::string& remote_name,
                      const std::string& local_ref_full_name_param,
                      const std::string& remote_ref_full_name_on_server_param,
                      bool force,
                      const std::string& token) {

    std::cout << "Attempting to push local '" << local_ref_full_name_param
              << "' to remote '" << remote_name << "' as '" << remote_ref_full_name_on_server_param
              << (force ? "' (force)" : "'") << std::endl;

    // --- 1. 参数规范化与本地引用解析 ---
    std::string local_ref_full_name = local_ref_full_name_param;
    if (!local_ref_full_name.starts_with("refs/") && local_ref_full_name != "HEAD") {
        local_ref_full_name = "refs/heads/" + local_ref_full_name;
    }
    std::string remote_ref_full_name_on_server = remote_ref_full_name_on_server_param;
    if (!remote_ref_full_name_on_server.starts_with("refs/")) {
        remote_ref_full_name_on_server = "refs/heads/" + remote_ref_full_name_on_server;
    }

    std::optional<std::string> local_tip_commit_hash_opt = _resolve_commit_ish_to_full_hash(local_ref_full_name);
    if (!local_tip_commit_hash_opt) {
        std::cerr << "Push Error: Local ref '" << local_ref_full_name << "' not found or invalid." << std::endl;
        return false;
    }
    std::string local_tip_hash = *local_tip_commit_hash_opt;
    std::cout << "  Local ref '" << local_ref_full_name << "' is at " << local_tip_hash.substr(0, 7) << std::endl;

    // --- 2. 获取远程配置并解析 URL ---
    std::optional<RemoteConfig> remote_cfg_opt = remote_get_config(remote_name);
    if (!remote_cfg_opt) {
        std::cerr << "Push Error: Remote '" << remote_name << "' not configured." << std::endl;
        return false;
    }
    std::string url = remote_cfg_opt->url;
    std::string host, port_str, server_repo_path_from_url;

    size_t colon_pos = url.find(':');
    size_t slash_pos = url.find('/', (colon_pos != std::string::npos ? colon_pos + 1 : 0));

    if (colon_pos == std::string::npos) {
        std::cerr << "Push Error: Invalid remote URL format (missing port): " << url << std::endl;
        return false;
    }
    host = url.substr(0, colon_pos);
    if (slash_pos != std::string::npos && slash_pos > colon_pos + 1) {
        port_str = url.substr(colon_pos + 1, slash_pos - (colon_pos + 1));
        server_repo_path_from_url = url.substr(slash_pos + 1);
    } else if (slash_pos == std::string::npos && url.length() > colon_pos + 1) {
        port_str = url.substr(colon_pos + 1);
        std::cerr << "Push Error: Remote URL must specify a repository path on server (e.g., host:port/path/to/repo): " << url << std::endl;
        return false;
    } else {
        std::cerr << "Push Error: Invalid remote URL format (could not parse port and path): " << url << std::endl;
        return false;
    }
    if (port_str.empty()){
        std::cerr << "Push Error: Port not found or empty in remote URL: " << url << std::endl;
        return false;
    }
    if (server_repo_path_from_url.empty() && url.back() != '/') {
        std::cerr << "Push Error: Repository path on server is missing or invalid in URL: " << url << std::endl;
        return false;
    }

    // --- 3. 网络操作 ---
    boost::asio::io_context client_io_ctx_push;
    RemoteClient client(client_io_ctx_push);

    if (!client.Connect(host, port_str)) {
        std::cerr << "Push Error: Failed to connect to remote server " << host << ":" << port_str << std::endl;
        return false;
    }
    std::cout << "  Connected to " << host << ":" << port_str << std::endl;

    if (!client.TargetRepository(token, server_repo_path_from_url)) { // <--- 传递 token
        std::cerr << "Push Error: Failed to target repository '" << server_repo_path_from_url << "' on server." << std::endl;
        client.Disconnect();
        return false;
    }
    std::cout << "  Successfully targeted remote repository '" << server_repo_path_from_url << "'." << std::endl;

    std::optional<std::map<std::string, std::string>> remote_refs_map_opt = client.ListRemoteRefs(token); // <--- 传递 token
    if (!remote_refs_map_opt) {
        std::cerr << "Push Error: Failed to list remote refs." << std::endl;
        client.Disconnect();
        return false;
    }
    std::string remote_tip_hash;
    auto it_remote_ref = remote_refs_map_opt->find(remote_ref_full_name_on_server);
    if (it_remote_ref != remote_refs_map_opt->end()) {
        remote_tip_hash = it_remote_ref->second;
        std::cout << "  Remote ref '" << remote_ref_full_name_on_server << "' currently at " << remote_tip_hash.substr(0, 7) << std::endl;
    } else {
        std::cout << "  Remote ref '" << remote_ref_full_name_on_server << "' does not exist on server. Will attempt to create." << std::endl;
    }

    // --- 4. 确定要发送的 Commits ---
    if (local_tip_hash == remote_tip_hash && !remote_tip_hash.empty()) {
        std::cout << "Everything up-to-date for '" << local_ref_full_name << "'." << std::endl;
        client.Disconnect();
        return true;
    }

    bool is_ff_push = remote_tip_hash.empty() || is_fast_forward(remote_tip_hash, local_tip_hash);
    std::vector<std::string> commits_to_send_hashes;

    if (is_ff_push) {
        std::cout << "  Push is fast-forward or new ref." << std::endl;
        if (!get_commits_between(local_tip_hash, remote_tip_hash, commits_to_send_hashes)) {
            std::cerr << "Push Error: Failed to determine commits for a fast-forward push." << std::endl;
            client.Disconnect();
            return false;
        }
    } else { // 不是 fast-forward
        if (!force) {
            std::cerr << "Push rejected: Update to '" << remote_ref_full_name_on_server
                      << "' (from " << (!remote_tip_hash.empty() ? remote_tip_hash.substr(0,7) : "non-existent")
                      << " to " << local_tip_hash.substr(0,7)
                      << ") is not a fast-forward. Fetch and merge/rebase first, or use --force." << std::endl;
            client.Disconnect();
            return false;
        }
        std::cout << "  Non-fast-forward push (force enabled)." << std::endl;
        std::optional<std::string> common_ancestor_opt = _find_common_ancestor(local_tip_hash, remote_tip_hash);
        if (!get_commits_between(local_tip_hash, common_ancestor_opt.value_or(""), commits_to_send_hashes)) {
             std::cerr << "Push Error: Failed to determine commits for a forced push." << std::endl;
             client.Disconnect();
             return false;
        }
    }
    if (commits_to_send_hashes.empty() && local_tip_hash != remote_tip_hash) {
        // 这种情况可能发生在强制推送到一个旧的、服务器已有的commit，或者创建一个全新的空分支（但local_tip_hash不应为空）
        std::cout << "  No new commit sequence to send, but ref tips differ. This might be a force push to an older/unrelated commit or creating an empty ref." << std::endl;
    }


    // --- 5. 收集对象 ---
    std::set<std::string> objects_to_potentially_send;
    if (!commits_to_send_hashes.empty()){
        collect_objects_for_commits(commits_to_send_hashes, objects_to_potentially_send);
    }
    // 确保目标 tip 本身及其树被包含，以防 commits_to_send_hashes 不包含它（例如，force 到一个单独的commit）
    if (objects_to_potentially_send.find(local_tip_hash) == objects_to_potentially_send.end() && !local_tip_hash.empty()) {
         std::set<std::string> visited_for_tip; // 临时的 visited set
         collect_objects_recursive_for_push(local_tip_hash, objects_to_potentially_send, visited_for_tip);
    }
    std::cout << "  Collected " << objects_to_potentially_send.size() << " unique objects for potential push." << std::endl;

    // --- 6. 检查服务器缺失的对象 ---
    std::vector<std::string> objects_to_upload_final_list;
    if (!objects_to_potentially_send.empty()) {
        std::vector<std::string> check_vec(objects_to_potentially_send.begin(), objects_to_potentially_send.end());
        std::vector<ObjectExistenceStatus> server_object_statuses;
        if (!client.CheckObjects(token, check_vec, server_object_statuses)) { // <--- 传递 token
            std::cerr << "Push Error: Failed to check objects on server." << std::endl;
            client.Disconnect();
            return false;
        }
        for (const auto& status : server_object_statuses) {
            if (!status.exists_on_server) {
                objects_to_upload_final_list.push_back(status.requested_hash);
            }
        }
        std::cout << "  Server needs " << objects_to_upload_final_list.size() << " new object(s)." << std::endl;
    } else if (local_tip_hash == remote_tip_hash) {
        // 已在前面处理
    } else {
        std::cout << "  No objects identified to send." << std::endl;
    }

    // --- 7. 上传缺失的对象 ---
    for (const std::string& hash_to_upload : objects_to_upload_final_list) {
        std::optional<std::vector<char>> raw_content_opt = get_raw_object_content(hash_to_upload);
        if (!raw_content_opt) { // 包括内容为空的情况，get_raw_object_content 应该返回 nullopt
            std::cerr << "Push Error: Could not read local object " << hash_to_upload.substr(0,7) << " for upload." << std::endl;
            client.Disconnect();
            return false;
        }
        if (!client.PutObject(token, hash_to_upload, *raw_content_opt)) { // <--- 传递 token
            std::cerr << "Push Error: Failed to upload object " << hash_to_upload.substr(0,7) << " to server." << std::endl;
            client.Disconnect();
            return false;
        }
        std::cout << "  Uploaded object " << hash_to_upload.substr(0, 7) << std::endl;
    }
    if (!objects_to_upload_final_list.empty()) {
        std::cout << "  All necessary objects uploaded." << std::endl;
    } else if (!objects_to_potentially_send.empty()){ // 有潜在对象但服务器都有
        std::cout << "  Server already has all necessary objects." << std::endl;
    }


    // --- 8. 请求更新远程引用 ---
    std::optional<std::string> expected_old_hash_for_server_check = std::nullopt;
    if (!force && is_ff_push && !remote_tip_hash.empty()) {
        expected_old_hash_for_server_check = remote_tip_hash;
    }

    std::cout << "  Requesting server to update ref '" << remote_ref_full_name_on_server
              << "' to " << local_tip_hash.substr(0, 7)
              << (expected_old_hash_for_server_check ? " (expecting old " + expected_old_hash_for_server_check->substr(0,7) + ")" : "")
              << (force ? " (force)" : "")
              << std::endl;

    std::vector<char> update_resp_body;
    uint16_t update_result_id = client.UpdateRef(token, remote_ref_full_name_on_server, // <--- 传递 token
                                                 local_tip_hash,
                                                 expected_old_hash_for_server_check,
                                                 force, // <--- 传递 force 标志
                                                 update_resp_body);

    bool success = false;
    std::string server_msg(update_resp_body.begin(), update_resp_body.end());
    server_msg.erase(std::remove(server_msg.begin(), server_msg.end(), '\0'), server_msg.end());

    if (update_result_id == Protocol::MSG_RESP_REF_UPDATED) {
        std::cout << "Push successful for '" << remote_ref_full_name_on_server << "'!" << std::endl;
        std::cout << "  " << (remote_tip_hash.empty() ? "[new ref]" : remote_tip_hash.substr(0,7) + ".." + local_tip_hash.substr(0,7))
                  << "  " << local_ref_full_name_param << " -> " << remote_ref_full_name_on_server_param << std::endl;
        success = true;
    } else if (update_result_id == Protocol::MSG_RESP_REF_UPDATE_DENIED) {
        std::cerr << "Push rejected by server for '" << remote_ref_full_name_on_server << "'.";
        if(!server_msg.empty()) std::cerr << " Reason: " << server_msg;
        std::cerr << std::endl;
    } else if (update_result_id == Protocol::MSG_RESP_AUTH_REQUIRED) {
         std::cerr << "Push failed for '" << remote_ref_full_name_on_server << "': Authentication required or token invalid." << std::endl;
    }
    else {
        std::cerr << "Push Error: Server failed to update remote ref '" << remote_ref_full_name_on_server
                  << "' (Response ID: " << update_result_id << ").";
        if(!server_msg.empty()) std::cerr << " Message: " << server_msg;
        std::cerr << std::endl;
    }

    client.Disconnect();
    return success;
}


/**
 * @brief 从指定的远程仓库获取更新，并更新本地的远程跟踪分支（ref/remotes/）
 * @param remote_name 要推送到的远程仓库的别名。
 * @param ref_to_fetch_param 默认为空 更新所有分支
*/
bool Repository::fetch(const std::string& remote_name,
                       const std::string& token,
                       const std::string& ref_to_fetch_param /* = "" */) {
    std::cout << "Fetching from remote '" << remote_name << "'";
    if (!ref_to_fetch_param.empty()) {
        std::cout << " (ref: " << ref_to_fetch_param << ")";
    }
    std::cout << "..." << std::endl;

    // --- 1. 获取远程配置并解析 URL ---
    std::optional<RemoteConfig> remote_cfg_opt = remote_get_config(remote_name);
    if (!remote_cfg_opt) {
        std::cerr << "Fetch Error: Remote '" << remote_name << "' not configured." << std::endl;
        return false;
    }
    std::string url = remote_cfg_opt->url;
    std::string host, port_str, server_repo_path_from_url;

    // (URL 解析逻辑 - 与您之前 push/clone 中的版本一致)
    size_t colon_pos = url.find(':');
    size_t slash_pos = url.find('/', (colon_pos != std::string::npos ? colon_pos + 1 : 0));
    if (colon_pos == std::string::npos) {
        std::cerr << "Fetch Error: Invalid remote URL format (missing port): " << url << std::endl; return false;
    }
    host = url.substr(0, colon_pos);
    if (slash_pos != std::string::npos && slash_pos > colon_pos + 1) { // host:port/path
        port_str = url.substr(colon_pos + 1, slash_pos - (colon_pos + 1));
        server_repo_path_from_url = url.substr(slash_pos + 1);
    } else if (slash_pos == std::string::npos && url.length() > colon_pos + 1) { // host:port (没有路径，通常用于根)
        port_str = url.substr(colon_pos + 1);
        // 对于 fetch，通常也需要一个仓库路径，除非服务器设计为 host:port 直接映射到一个仓库
        // 为了与 push/clone 一致，这里也要求有路径，或者 URL 以 '/' 结尾表示根仓库
        if (url.back() != '/') { // 简化：如果URL不是 host:port/，则认为缺少仓库路径
             std::cerr << "Fetch Error: Remote URL must specify a repository path or end with '/': " << url << std::endl; return false;
        }
        // server_repo_path_from_url 会是空字符串，如果服务器支持，这可能指向其根下的默认仓库
    } else { // 格式无效
        std::cerr << "Fetch Error: Invalid remote URL format (could not parse port and path): " << url << std::endl; return false;
    }
    if (port_str.empty()){
        std::cerr << "Fetch Error: Port not found or empty in remote URL: " << url << std::endl; return false;
    }
    // server_repo_path_from_url 可以为空，如果URL以'/'结尾，表示服务器上的根路径仓库（如果服务器支持）


    // --- 2. 网络操作：连接并选定目标仓库 ---
    boost::asio::io_context client_io_ctx_fetch;
    RemoteClient client(client_io_ctx_fetch);

    if (!client.Connect(host, port_str)) {
        std::cerr << "Fetch Error: Failed to connect to remote server " << host << ":" << port_str << std::endl;
        return false;
    }
    std::cout << "  Connected to " << host << ":" << port_str << std::endl;

    // TargetRepository 现在不需要 Token
    if (!client.TargetRepository(token, server_repo_path_from_url)) {
        // RemoteClient::TargetRepository 内部应已打印错误（包括认证失败，如果它也检查的话）
        // 或者这里根据返回的特定错误ID判断是否是认证失败
        std::cerr << "Fetch Error: Failed to target repository '" << server_repo_path_from_url << "' on server." << std::endl;
        client.Disconnect();
        return false;
    }
    std::cout << "  Successfully targeted remote repository '" << server_repo_path_from_url << "'." << std::endl;

    // --- 3. 获取远程所有引用 (或特定引用) ---
    // ListRemoteRefs 现在需要 Token
    std::optional<std::map<std::string, std::string>> all_remote_refs_map_opt = client.ListRemoteRefs(token);
    if (!all_remote_refs_map_opt) {
        // ListRemoteRefs 内部或 SendAndReceive 会打印认证失败或网络错误
        std::cerr << "Fetch Error: Failed to list remote refs." << std::endl;
        client.Disconnect();
        return false;
    }
    std::map<std::string, std::string>& all_remote_refs_map = *all_remote_refs_map_opt;
    if (all_remote_refs_map.empty() && ref_to_fetch_param.empty()) {
        std::cout << "  Remote repository '" << remote_name << "' has no refs to fetch." << std::endl;
        client.Disconnect();
        return true; // 成功连接并获取了空的引用列表
    }
    std::cout << "  Received " << all_remote_refs_map.size() << " refs from remote." << std::endl;

    // --- 3c. 处理并存储远程 HEAD 信息 ---
    auto remote_head_iter = all_remote_refs_map.find("HEAD");
    if (remote_head_iter != all_remote_refs_map.end()) {
        const std::string& remote_head_content = remote_head_iter->second; // 例如 "ref: refs/heads/main"
        std::filesystem::path local_remote_head_path = mygit_dir_ / "refs" / "remotes" / remote_name / "HEAD";
        std::error_code ec_head_write;
        std::filesystem::path remote_head_parent_dir = local_remote_head_path.parent_path();

        if (!std::filesystem::exists(remote_head_parent_dir, ec_head_write)) {
            if (!std::filesystem::create_directories(remote_head_parent_dir, ec_head_write)) {
                std::cerr << "Fetch Warning: Failed to create directory for remote HEAD: " << remote_head_parent_dir.string() << " - " << ec_head_write.message() << std::endl;
            }
        } else if (!std::filesystem::is_directory(remote_head_parent_dir, ec_head_write)) {
            std::cerr << "Fetch Warning: Path for remote HEAD parent is not a directory: " << remote_head_parent_dir.string() << std::endl;
        }

        if (!ec_head_write) { // 只有在父目录操作无误时才尝试写入
            std::ofstream ofs_remote_head(local_remote_head_path, std::ios::trunc);
            if (ofs_remote_head.is_open()) {
                ofs_remote_head << remote_head_content << std::endl; // Git 通常会加换行
                ofs_remote_head.close();
                if(ofs_remote_head.good()){
                    std::cout << "  Stored remote HEAD (" << remote_head_content << ") locally for '" << remote_name << "'." << std::endl;
                } else {
                    std::cerr << "Fetch Warning: Failed to write remote HEAD information to " << local_remote_head_path.string() << std::endl;
                }
            } else {
                std::cerr << "Fetch Warning: Failed to open remote HEAD file for writing: " << local_remote_head_path.string() << std::endl;
            }
        } else { // 捕获之前 exists/create_directories/is_directory 的错误
             std::cerr << "Fetch Warning: Filesystem error with parent directory for remote HEAD " << remote_head_parent_dir.string() << ": " << ec_head_write.message() << std::endl;
        }
    }

    // --- 3d. 如果指定了特定 ref，则筛选远程引用 ---
    std::map<std::string, std::string> refs_to_process_on_remote;
    if (!ref_to_fetch_param.empty()) {
        std::string full_ref_name_on_remote_to_find = ref_to_fetch_param;
        // 尝试解析用户提供的短名称 (例如 "main" -> "refs/heads/main")
        if (!full_ref_name_on_remote_to_find.starts_with("refs/")) {
            std::string potential_branch = "refs/heads/" + ref_to_fetch_param;
            std::string potential_tag = "refs/tags/" + ref_to_fetch_param;
            if (all_remote_refs_map.count(potential_branch)) {
                full_ref_name_on_remote_to_find = potential_branch;
            } else if (all_remote_refs_map.count(potential_tag)) {
                full_ref_name_on_remote_to_find = potential_tag;
            } else {
                // 如果用户指定了不在 "refs/" 下的名称，并且在远程的heads或tags下都找不到，则报错
                std::cerr << "Fetch Error: Specified ref '" << ref_to_fetch_param
                          << "' not found as a branch or tag on remote '" << remote_name << "'." << std::endl;
                client.Disconnect();
                return false;
            }
        }

        auto it = all_remote_refs_map.find(full_ref_name_on_remote_to_find);
        if (it != all_remote_refs_map.end()) {
            refs_to_process_on_remote[it->first] = it->second;
            std::cout << "  Targeting specific ref for fetch: " << it->first << " -> " << it->second.substr(0,7) << std::endl;
        } else {
            std::cerr << "Fetch Error: Specified ref '" << full_ref_name_on_remote_to_find << "' not found on remote '" << remote_name << "'." << std::endl;
            client.Disconnect();
            return false;
        }
    } else { // 如果没有指定特定 ref，则处理所有非 "HEAD" 的引用
        for(const auto& pair : all_remote_refs_map){
            if(pair.first != "HEAD"){ // "HEAD" 引用已被特殊处理，不作为普通分支或标签fetch
                refs_to_process_on_remote[pair.first] = pair.second;
            }
        }
    }
    if (refs_to_process_on_remote.empty()) {
        std::cout << "  No specific refs to process after filtering." << std::endl;
        if (!ref_to_fetch_param.empty()){ // 如果是指定了引用但筛选后为空，这可能是个问题
             std::cerr << "Fetch Error: Specified ref '"<< ref_to_fetch_param << "' led to no processable refs." << std::endl;
             client.Disconnect();
             return false;
        }
        client.Disconnect();
        return true; // 如果是 fetch all 且结果为空（除了HEAD），则正常结束
    }

    // --- 4. 确定需要下载的对象 (基于递归下载) ---
    std::vector<std::pair<std::filesystem::path, std::string>> refs_to_update_locally_fs_path;
    std::set<std::string> processing_queue_set; // 用于避免重复加入处理队列
    std::queue<std::string> object_processing_queue;

    for (const auto& remote_ref_pair : refs_to_process_on_remote) {
        const std::string& remote_ref_full_name = remote_ref_pair.first; // 例如 "refs/heads/main", "refs/tags/v1.0"
        const std::string& remote_tip_hash = remote_ref_pair.second;     // 该引用指向的 commit/tag 对象哈希
        std::filesystem::path local_equivalent_ref_path_fs;

        // 构造本地对应的远程跟踪引用路径或标签路径
        if (remote_ref_full_name.starts_with("refs/heads/")) {
            std::string branch_name = remote_ref_full_name.substr(std::string("refs/heads/").length());
            local_equivalent_ref_path_fs = mygit_dir_ / "refs" / "remotes" / remote_name / branch_name;
        } else if (remote_ref_full_name.starts_with("refs/tags/")) {
            std::string tag_name = remote_ref_full_name.substr(std::string("refs/tags/").length());
            local_equivalent_ref_path_fs = get_tags_directory() / tag_name;
        } else { // 其他类型的引用 (例如 refs/notes/*, refs/stash，或自定义引用)
            if (ref_to_fetch_param.empty()) { // 只有在 fetch all 时才跳过
                 std::cout << "  Skipping unknown type remote ref: " << remote_ref_full_name << std::endl;
            }
            continue; // 跳过不处理的引用类型
        }

        // 读取本地当前跟踪的哈希
        std::string local_current_tip_hash_for_ref;
        std::error_code ec_ref_read_local;
        if (std::filesystem::exists(local_equivalent_ref_path_fs, ec_ref_read_local) &&
            std::filesystem::is_regular_file(local_equivalent_ref_path_fs, ec_ref_read_local)) {
            std::ifstream ifs(local_equivalent_ref_path_fs);
            ifs >> local_current_tip_hash_for_ref;
            if (local_current_tip_hash_for_ref.length() != 40) local_current_tip_hash_for_ref.clear(); // 无效则清空
        }
        if (ec_ref_read_local) { // 记录文件系统错误
            std::cerr << "Fetch Warning: Error checking local ref " << local_equivalent_ref_path_fs.string() << ": " << ec_ref_read_local.message() << std::endl;
        }

        // 如果远程哈希与本地记录的哈希相同，则此引用已是最新
        if (remote_tip_hash == local_current_tip_hash_for_ref && !remote_tip_hash.empty()) {
            // std::cout << "  Ref '" << remote_ref_full_name << "' is already up-to-date at " << remote_tip_hash.substr(0,7) << std::endl;
            continue;
        }

        std::cout << "  Queuing updates for ref '" << remote_ref_full_name << "': "
                  << (local_current_tip_hash_for_ref.empty() ? "[new]" : local_current_tip_hash_for_ref.substr(0,7) + "..")
                  << remote_tip_hash.substr(0,7) << std::endl;

        // 标记这个引用需要在本地更新其指向的哈希
        refs_to_update_locally_fs_path.push_back({local_equivalent_ref_path_fs, remote_tip_hash});

        // 将远程的 tip 哈希加入对象处理队列 (如果尚未加入)
        if (!remote_tip_hash.empty() && processing_queue_set.find(remote_tip_hash) == processing_queue_set.end()) {
            object_processing_queue.push(remote_tip_hash);
            processing_queue_set.insert(remote_tip_hash);
        }
    }

    // 如果没有任何引用需要更新，并且对象队列也是空的，则说明一切都是最新的
    if (object_processing_queue.empty() && refs_to_update_locally_fs_path.empty()) {
        std::cout << (ref_to_fetch_param.empty() ? "Everything up-to-date." : "Specified ref '" + ref_to_fetch_param + "' is up-to-date or no objects to fetch.") << std::endl;
        client.Disconnect();
        return true;
    }

    int downloaded_count = 0;
    int processed_in_queue_count = 0;
    bool critical_download_error = false;

    // 循环处理对象队列，下载缺失的对象及其依赖
    while (!object_processing_queue.empty() && !critical_download_error) {
        std::string current_hash_to_process = object_processing_queue.front();
        object_processing_queue.pop();
        processed_in_queue_count++;

        // 如果对象已在本地存在，则不需要下载，但仍需解析它以将其依赖加入队列
        if (object_exists(current_hash_to_process)) {
            // std::cout << "  Object " << current_hash_to_process.substr(0,7) << " already exists locally." << std::endl;
        } else { // 对象本地不存在，需要从服务器下载
            std::string received_hash_from_server;
            std::vector<char> raw_object_content_char_vec;
            // GetObject 方法现在需要 token
            uint16_t get_obj_status = client.GetObject(token, current_hash_to_process, received_hash_from_server, raw_object_content_char_vec);

            if (get_obj_status == Protocol::MSG_RESP_OBJECT_CONTENT) {
                if (current_hash_to_process == received_hash_from_server) { // 校验返回的哈希是否与请求的一致
                    if (!write_raw_object(current_hash_to_process, raw_object_content_char_vec.data(), static_cast<uint32_t>(raw_object_content_char_vec.size()))) {
                        std::cerr << "Fetch Error: Failed to write downloaded object " << current_hash_to_process.substr(0,7) << " to local repository." << std::endl;
                        critical_download_error = true; // 关键对象写入失败，可能需要中止
                        continue;
                    }
                    // std::cout << "  Fetched object " << current_hash_to_process.substr(0,7) << std::endl; // 日志移到最后汇总
                    downloaded_count++;
                } else { // 哈希不匹配
                    std::cerr << "Fetch Error: Hash mismatch for downloaded object. Requested " << current_hash_to_process.substr(0,7)
                              << ", server responded for " << received_hash_from_server.substr(0,7) << std::endl;
                    critical_download_error = true; continue;
                }
            } else if (get_obj_status == Protocol::MSG_RESP_AUTH_REQUIRED) {
                 std::cerr << "Fetch Error: Authentication required or token invalid during GetObject for " << current_hash_to_process.substr(0,7) << std::endl;
                 critical_download_error = true; continue;
            } else { // MSG_RESP_OBJECT_NOT_FOUND 或其他错误
                std::cerr << "Fetch Error: Failed to get object " << current_hash_to_process.substr(0,7) << " from server. Status: " << get_obj_status << std::endl;
                critical_download_error = true; // 无法获取关键对象
                continue;
            }
        }

        // 对象现在应该在本地了（无论是刚下载的还是原先就有的），解析它并将其依赖加入队列
        std::optional<std::filesystem::path> object_file_path_opt = _find_object_file_by_prefix(current_hash_to_process);
        if (!object_file_path_opt) {
             std::cerr << "Fetch Critical Error: Object " << current_hash_to_process.substr(0,7) << " should exist locally but not found after download/check." << std::endl;
             critical_download_error = true; // 无法继续解析依赖
             continue;
        }

        // 使用 read_and_parse_object_file_content 获取类型和原始内容 (不含Git对象头)
        // 注意：这个函数返回的 raw_content_data_byte_vec 是去除了 "type size\0" 之后的内容
        std::optional<std::tuple<std::string, size_t, std::vector<std::byte>>> parsed_object_opt =
            read_and_parse_object_file_content(*object_file_path_opt);

        if (!parsed_object_opt) {
            std::cerr << "Fetch Warning: Failed to read/parse local object " << current_hash_to_process.substr(0,7) << " to find its dependencies." << std::endl;
            continue; // 跳过此对象的依赖解析，但可能导致不完整
        }
        const auto& [type_str_read, content_size_from_header, actual_content_data_byte_vec] = *parsed_object_opt;

        // 根据对象类型，将其引用的其他对象哈希加入队列
        if (type_str_read == Commit::type_str()) {
            // Commit::deserialize 需要的是去除头部 ("commit size\0") 后的内容
            auto commit_obj_opt = Commit::deserialize(actual_content_data_byte_vec);
            if (commit_obj_opt) {
                // 将 Tree 加入队列
                if (!commit_obj_opt->tree_hash_hex.empty() && processing_queue_set.find(commit_obj_opt->tree_hash_hex) == processing_queue_set.end()) {
                    object_processing_queue.push(commit_obj_opt->tree_hash_hex);
                    processing_queue_set.insert(commit_obj_opt->tree_hash_hex);
                }
                // 将所有父 Commit 加入队列
                for (const auto& parent_hash : commit_obj_opt->parent_hashes_hex) {
                    if (!parent_hash.empty() && processing_queue_set.find(parent_hash) == processing_queue_set.end()) {
                        object_processing_queue.push(parent_hash);
                        processing_queue_set.insert(parent_hash);
                    }
                }
            }
        } else if (type_str_read == Tree::type_str()) {
            // Tree::deserialize 需要的是去除头部 ("tree size\0") 后的内容
            auto tree_obj_opt = Tree::deserialize(actual_content_data_byte_vec);
            if (tree_obj_opt) {
                for (const auto& entry : tree_obj_opt->entries) {
                    // Tree 条目中的哈希已经是其他对象的哈希 (Tree 或 Blob)
                    if (!entry.sha1_hash_hex.empty() && processing_queue_set.find(entry.sha1_hash_hex) == processing_queue_set.end()) {
                        object_processing_queue.push(entry.sha1_hash_hex);
                        processing_queue_set.insert(entry.sha1_hash_hex);
                    }
                }
            }
        }
        // Blob 对象没有其他 Git 对象依赖，不需要进一步操作
    } // end while object_processing_queue

    if (downloaded_count > 0) {
        std::cout << "  Downloaded " << downloaded_count << " new object(s)." << std::endl;
    } else if (processed_in_queue_count > 0 && !refs_to_update_locally_fs_path.empty()) { // 只有当有引用要更新时，才说“无需下载”
         std::cout << "  No new objects needed to be downloaded. All " << processed_in_queue_count << " relevant objects already exist locally." << std::endl;
    } else if (refs_to_update_locally_fs_path.empty()){
         // 如果没有引用要更新，说明之前已经 up-to-date 了
    }


    // --- 7. 更新本地的远程跟踪引用文件 ---
    bool all_ref_updates_succeeded = true;
    if (critical_download_error && !refs_to_update_locally_fs_path.empty()){
        std::cerr << "Fetch Error: Due to critical object download errors, local refs will not be updated to potentially inconsistent states." << std::endl;
        all_ref_updates_succeeded = false;
    } else {
        for (const auto& ref_update_pair : refs_to_update_locally_fs_path) {
            const std::filesystem::path& local_ref_file_fs = ref_update_pair.first;
            const std::string& new_tip_hash = ref_update_pair.second; // 这是从服务器获取的最新哈希

            std::error_code ec_ref_write_parent;
            std::filesystem::path parent_dir_fs = local_ref_file_fs.parent_path();
            // 确保父目录存在 (例如 .biogit/refs/remotes/origin/)
            if (!std::filesystem::exists(parent_dir_fs, ec_ref_write_parent)) {
                if (!std::filesystem::create_directories(parent_dir_fs, ec_ref_write_parent)) {
                    std::cerr << "Fetch Error: Failed to create directory for ref: " << parent_dir_fs.string() << " - " << ec_ref_write_parent.message() << std::endl;
                    all_ref_updates_succeeded = false;
                    continue; // 跳过这个引用的更新
                }
            } else if (!std::filesystem::is_directory(parent_dir_fs, ec_ref_write_parent)){ // 如果存在但不是目录
                 std::cerr << "Fetch Error: Path for ref parent is not a directory: " << parent_dir_fs.string() << std::endl;
                 all_ref_updates_succeeded = false;
                 continue;
            }
            if(ec_ref_write_parent) { // 捕获 exists 或 is_directory 的其他文件系统错误
                std::cerr << "Fetch Error: Filesystem error checking parent directory " << parent_dir_fs.string() << ": " << ec_ref_write_parent.message() << std::endl;
                all_ref_updates_succeeded = false;
                continue;
            }

            // 覆盖写入本地引用文件
            std::ofstream ofs(local_ref_file_fs, std::ios::trunc);
            if (!ofs.is_open()) {
                std::cerr << "Fetch Error: Failed to open local ref file for writing: " << local_ref_file_fs.string() << std::endl;
                all_ref_updates_succeeded = false;
                continue;
            }
            ofs << new_tip_hash << std::endl; // Git 的引用文件通常以换行结束
            ofs.close();
            if (!ofs.good()) { // 检查写入和关闭操作是否都成功
                std::cerr << "Fetch Error: Failed to write or close local ref file: " << local_ref_file_fs.string() << std::endl;
                all_ref_updates_succeeded = false;
            } else {
                // 尝试获得相对于 .biogit 目录的引用名，以便更友好地显示
                std::string display_ref_name = local_ref_file_fs.filename().string(); // 默认文件名
                try { // relative 操作在某些情况下可能抛异常
                    std::filesystem::path relative_to_mygit = std::filesystem::relative(local_ref_file_fs, mygit_dir_);
                    display_ref_name = relative_to_mygit.generic_string();
                } catch (const std::exception& e_rel) {
                    // 保持使用文件名
                }
                std::cout << "  Updated local ref '" << display_ref_name << "' to " << new_tip_hash.substr(0,7) << std::endl;
            }
        }
    }

    client.Disconnect();
    std::cout << "Fetch completed." << (all_ref_updates_succeeded ? "" : " With some errors updating local refs.") << std::endl;
    return all_ref_updates_succeeded;
}


/**
 * @brief 从指定的远程仓库获取更新，并将其合并到当前检出的本地分支。
 * @param remote_name 要从中拉取更新的远程仓库的名称 (例如 "origin")。
 * @param remote_branch_name 要从远程仓库拉取的特定分支的名称 (例如 "main")。此分支的更新将被合并到当前本地分支。
 * @return 如果整个 pull 操作（包括 fetch 和 merge）成功完成，则返回 true。
 * @details
 * 首先执行一个 fetch 操作，以下载远程仓库中指定分支的最新提交和相关对象，
 * 并更新本地对应的远程跟踪分支。然后，它尝试将这个更新后的远程跟踪分支合并到当前活动的本地分支。
 */
bool Repository::pull(const std::string& remote_name,
                      const std::string& remote_branch_name,
                      const std::string& token) {
    std::cout << "Pulling from remote '" << remote_name << "', branch '" << remote_branch_name << "'..." << std::endl;

    // --- 0. 前提条件检查 ---
    // a. 检查工作区是否干净 (这是 merge 的前提条件之一)
    if (!is_workspace_clean()) {
        std::cerr << "Pull Error: Your local changes to the following files would be overwritten by merge." << std::endl;
        // 可以调用 status() 来更详细地列出未提交的更改，或者一个返回结构化数据的status变体
        status(); // 简单地调用 status() 以显示问题
        std::cerr << "Please commit your changes or stash them before you pull." << std::endl;
        return false;
    }

    // b. 检查当前是否在某个本地分支上 (HEAD 是否是符号引用到 refs/heads/)
    //    并获取当前本地分支的名称。
    std::filesystem::path head_file_path = get_head_file_path();
    std::string current_local_branch_name;
    bool is_on_a_branch = false;

    if (std::filesystem::exists(head_file_path)) {
        std::ifstream head_ifs(head_file_path);
        std::string head_content_line;
        if (std::getline(head_ifs, head_content_line)) {
            // 清理行尾的换行符
            if (!head_content_line.empty() && (head_content_line.back() == '\n' || head_content_line.back() == '\r')) head_content_line.pop_back();
            if (!head_content_line.empty() && head_content_line.back() == '\r') head_content_line.pop_back();

            const std::string ref_prefix = "ref: refs/heads/";
            if (head_content_line.rfind(ref_prefix, 0) == 0) { // starts_with
                current_local_branch_name = head_content_line.substr(ref_prefix.length());
                is_on_a_branch = true;
            }
        }
        head_ifs.close();
    }

    if (!is_on_a_branch || current_local_branch_name.empty()) {
        std::cerr << "Pull Error: You are not currently on a local branch. ";
        if (current_local_branch_name.empty() && is_on_a_branch){ // HEAD 是 ref: refs/heads/ 但分支名为空 (不太可能)
             std::cerr << "HEAD points to an invalid local branch ref." << std::endl;
        } else { // 分离 HEAD 状态
             std::cerr << "HEAD is detached." << std::endl;
        }
        std::cerr << "Please switch to a local branch (e.g., 'biogit2 switch <branch_name>') to pull changes into it." << std::endl;
        return false;
    }
    std::cout << "  Currently on local branch '" << current_local_branch_name << "'." << std::endl;

    // --- 1. 执行 fetch 操作 ---
    // fetch 指定的远程分支。
    // fetch 方法会更新本地的 .biogit/refs/remotes/<remote_name>/<remote_branch_name>
    // 并下载所有必要的对象。
    std::cout << "\n  Running fetch for 'origin/" << remote_branch_name << "'..." << std::endl;
    if (!fetch(remote_name, token, remote_branch_name)) { // 传递 token 和要 fetch 的特定分支
        std::cerr << "Pull Error: Fetch operation failed." << std::endl;
        // fetch 方法内部应该已经打印了更具体的错误 (例如认证失败、连接失败)
        return false;
    }
    std::cout << "  Fetch completed.\n" << std::endl;

    // --- 2. 确定要合并的远程跟踪分支的名称 ---
    //    fetch 操作会更新例如 refs/remotes/origin/main 这样的引用。
    //    我们需要将这个名称 (例如 "origin/main") 传递给 merge 方法。
    //    Repository::_resolve_commit_ish_to_full_hash 需要能正确解析这种格式。
    std::string remote_tracking_branch_to_merge = remote_name + "/" + remote_branch_name;
    std::cout << "  Preparing to merge remote-tracking branch '" << remote_tracking_branch_to_merge
              << "' into local branch '" << current_local_branch_name << "'." << std::endl;

    // 2a. 检查远程跟踪分支是否存在且有效
    std::optional<std::string> remote_tracking_branch_tip_hash = _resolve_commit_ish_to_full_hash(remote_tracking_branch_to_merge);
    if (!remote_tracking_branch_tip_hash) {
        std::cerr << "Pull Error: Could not resolve the fetched remote-tracking branch '" << remote_tracking_branch_to_merge
                  << "'. It might not exist locally after fetch, or fetch did not update it correctly." << std::endl;
        return false;
    }
    // 2b. (可选) 检查与当前本地分支的 HEAD 是否相同 (如果相同则已是最新)
    std::optional<std::string> current_local_head_hash = _get_head_commit_hash();
    if (current_local_head_hash && remote_tracking_branch_tip_hash && *current_local_head_hash == *remote_tracking_branch_tip_hash) {
        std::cout << "Already up to date." << std::endl;
        return true; // 无需合并
    }


    // --- 3. 执行 merge 操作 ---
    //    调用 Repository::merge 方法。
    //    merge 方法会处理 fast-forward、三路合并以及冲突。
    if (!merge(remote_tracking_branch_to_merge)) {
        // merge 方法内部会在失败或冲突时打印具体信息
        std::cerr << "Pull Error: Merge operation failed or resulted in conflicts." << std::endl;
        if (std::filesystem::exists(mygit_dir_ / MERGE_HEAD_FILE_NAME)) { // 检查是否有冲突标记文件
             std::cout << "Automatic merge failed; fix conflicts and then commit the result." << std::endl;
        }
        return false; // pull 失败
    }

    std::cout << "Pull successful for branch '" << current_local_branch_name << "' from '" << remote_name << "/" << remote_branch_name << "'." << std::endl;
    return true;
}



/**
 * @brief 克隆一个远程仓库到指定的本地目录。
 * @param remote_url 要克隆的远程仓库的 URL (例如 "localhost:10088/project")。
 * @param target_directory_path 要在本地创建并克隆到的目录的路径。
 * @return 如果克隆成功，返回一个包含新创建的 Repository 对象的 std::optional。
 * @details
 * 该过程包括：
 * 1. 在本地目标路径下初始化一个新的 BioGit 仓库。
 * 2. 将给定的远程 URL 添加为名为 "origin" 的远程配置。
 * 3. 执行 fetch 操作，从 "origin" 下载所有对象和引用。
 * 4. 确定远程仓库的默认分支 (通常通过远程的 HEAD 引用)。
 * 5. 在本地创建并检出 (switch to) 一个与远程默认分支同名的本地分支，
 * 并设置其跟踪远程分支。工作目录和索引将被填充以匹配该分支的状态。
 */
std::optional<Repository> Repository::clone(
    const std::string& remote_url_str,
    const std::filesystem::path& target_directory_path) {

    std::cout << "正在克隆 '" << remote_url_str << "' 到 '" << target_directory_path.string() << "'..." << std::endl;
    std::error_code ec;

    // --- 步骤 0: 检查和创建目标目录 ---
    if (std::filesystem::exists(target_directory_path, ec)) {
        if (std::filesystem::is_directory(target_directory_path, ec)) {
            if (!std::filesystem::is_empty(target_directory_path, ec)) {
                std::cerr << "错误: 目标路径 '" << target_directory_path.string() << "' 已存在且不为空。" << std::endl;
                return std::nullopt;
            }
        } else {
            std::cerr << "错误: 目标路径 '" << target_directory_path.string() << "' 已存在但不是一个目录。" << std::endl;
            return std::nullopt;
        }
    } else {
        if (!std::filesystem::create_directories(target_directory_path, ec)) {
            std::cerr << "错误: 无法创建目标目录 '" << target_directory_path.string() << "': " << ec.message() << std::endl;
            return std::nullopt;
        }
    }
    if (ec) {
        std::cerr << "错误: 处理目标目录 '" << target_directory_path.string() << "' 时出错: " << ec.message() << std::endl;
        return std::nullopt;
    }

    // --- 步骤 1: 在目标目录初始化本地仓库 ---
    std::optional<Repository> cloned_repo_opt = Repository::init(target_directory_path);
    if (!cloned_repo_opt) {
        std::cerr << "错误: 在 '" << target_directory_path.string() << "' 中初始化仓库失败。" << std::endl;
        return std::nullopt;
    }
    Repository cloned_repo = std::move(*cloned_repo_opt);
    std::cout << "  在新目录 '" << target_directory_path.string() << "' 中初始化了空的 BioGit 仓库。" << std::endl;

    // --- 步骤 2: 添加远程 'origin' ---
    if (!cloned_repo.remote_add("origin", remote_url_str)) {
        std::cerr << "错误: 添加远程 'origin' (" << remote_url_str << ") 失败。" << std::endl;
        return std::nullopt;
    }
    std::cout << "  已将远程 'origin' 设置为 '" << remote_url_str << "'。" << std::endl;

    // --- 步骤 3 & 4: 使用预设的 "克隆专用" 用户凭证获取 Token，并用此 Token 执行 fetch ---
    std::cout << "\n  尝试使用预设凭证获取认证 Token 以进行数据下载..." << std::endl;

    // **重要**: 这里的用户名和密码是预设的，用于 clone 操作。
    const std::string CLONE_USERNAME = "cloneuser"; // 预设的克隆用户名
    const std::string CLONE_PASSWORD = "clonepassword"; // 预设的克隆密码

    boost::asio::io_context login_io_ctx;
    RemoteClient login_client(login_io_ctx);

    // 从 remote_url_str 解析 host 和 port 以便连接登录
    std::string host_for_login, port_str_for_login, dummy_path; // login不需要服务器仓库路径
    size_t colon_pos_login = remote_url_str.find(':');
    size_t slash_pos_login = remote_url_str.find('/', (colon_pos_login != std::string::npos ? colon_pos_login + 1 : 0));

    if (colon_pos_login == std::string::npos) {
        std::cerr << "错误 (clone-login): 远程 URL 格式无效 (缺少端口): " << remote_url_str << std::endl;
        return std::nullopt;
    }
    host_for_login = remote_url_str.substr(0, colon_pos_login);
    if (slash_pos_login != std::string::npos && slash_pos_login > colon_pos_login + 1) {
        port_str_for_login = remote_url_str.substr(colon_pos_login + 1, slash_pos_login - (colon_pos_login + 1));
    } else if (slash_pos_login == std::string::npos && remote_url_str.length() > colon_pos_login + 1) {
        port_str_for_login = remote_url_str.substr(colon_pos_login + 1);
    } else { // 无法解析出 host:port
        std::cerr << "错误 (clone-login): 无法从 URL 解析主机和端口: " << remote_url_str << std::endl;
        return std::nullopt;
    }
    if (host_for_login.empty() || port_str_for_login.empty()){
         std::cerr << "错误 (clone-login): 从 URL 解析出的主机或端口为空: " << remote_url_str << std::endl;
        return std::nullopt;
    }


    if (!login_client.Connect(host_for_login, port_str_for_login)) {
        std::cerr << "错误: 无法连接到服务器 " << host_for_login << ":" << port_str_for_login << " 以获取克隆用 Token。" << std::endl;
        return std::nullopt;
    }

    std::string clone_session_token;
    std::string login_server_message;
    if (!login_client.LoginUser(CLONE_USERNAME, CLONE_PASSWORD, clone_session_token, login_server_message)) {
        std::cerr << "错误: 使用预设克隆用户 '" << CLONE_USERNAME << "' 登录失败。 " << login_server_message << std::endl;
        login_client.Disconnect();
        return std::nullopt;
    }
    login_client.Disconnect(); // 登录获取 Token 后即可断开
    std::cout << "  已使用预设凭证成功获取临时认证 Token。" << std::endl;

    // 使用获取到的 clone_session_token 执行 fetch
    std::cout << "\n  正在使用临时 Token 从远程 'origin' 获取数据..." << std::endl;
    if (!cloned_repo.fetch("origin",  clone_session_token, "")) { // 第三个参数 "" 表示 fetch 所有引用
        std::cerr << "错误: 从远程 'origin' 获取数据失败（即使使用了临时Token）。" << std::endl;
        return std::nullopt;
    }
    std::cout << "  获取数据完成。\n" << std::endl;

    // --- 步骤 5: 检出远程仓库的默认分支/状态 ---
    // (这部分的逻辑与您之前的版本相同，它读取本地存储的远程 HEAD 信息，
    //  创建本地分支，更新HEAD、Index、Workdir，设置上游)
    std::filesystem::path local_stored_remote_head_path = cloned_repo.get_mygit_directory() / "refs" / "remotes" / "origin" / "HEAD";
    std::string remote_default_branch_name_fallback = "main";
    std::string target_checkout_identifier = remote_default_branch_name_fallback;
    bool is_target_a_commit_hash = false;

    if(std::filesystem::exists(local_stored_remote_head_path)){ /* ... 解析远程HEAD ... */
        std::ifstream rth_ifs(local_stored_remote_head_path); std::string line;
        if(std::getline(rth_ifs, line)){
            if (!line.empty() && (line.back()=='\n'||line.back()=='\r')) line.pop_back(); if (!line.empty() && line.back()=='\r') line.pop_back();
            std::string ref_prefix = "ref: refs/heads/";
            if (line.rfind(ref_prefix,0)==0) { target_checkout_identifier = line.substr(ref_prefix.length()); is_target_a_commit_hash = false; std::cout << "  远程HEAD指向分支 '" << target_checkout_identifier << "'。" << std::endl;}
            else if (line.length()==40 && std::all_of(line.begin(),line.end(),::isxdigit)) { target_checkout_identifier=line; is_target_a_commit_hash=true; std::cout << "  远程HEAD分离，指向commit " <<target_checkout_identifier.substr(0,7)<<"."<<std::endl;}
            else {std::cout << "  警告: 无法解析远程HEAD ('"<<line<<"')。尝试默认 '"<<remote_default_branch_name_fallback<<"'。" << std::endl; target_checkout_identifier = remote_default_branch_name_fallback; is_target_a_commit_hash = false;}
        } rth_ifs.close();
    } else {std::cout << "  未找到本地存储的远程HEAD。尝试默认 '"<<remote_default_branch_name_fallback<<"'。" << std::endl; target_checkout_identifier = remote_default_branch_name_fallback; is_target_a_commit_hash = false;}

    std::string final_commit_to_checkout_hash;
    if (is_target_a_commit_hash) { final_commit_to_checkout_hash = target_checkout_identifier; }
    else {
        std::optional<std::string> remote_branch_tip = cloned_repo._resolve_commit_ish_to_full_hash("origin/" + target_checkout_identifier);
        if (!remote_branch_tip) { std::cerr << "错误: 无法找到 'origin/" << target_checkout_identifier << "' 的commit哈希。" << std::endl; return cloned_repo; }
        final_commit_to_checkout_hash = *remote_branch_tip;
    }

    std::cout << "  正在检出 commit " << final_commit_to_checkout_hash.substr(0,7) << "..." << std::endl;
    auto commit_obj = Commit::load_by_hash(final_commit_to_checkout_hash, cloned_repo.get_objects_directory());
    if (!commit_obj) { std::cerr << "错误: 无法加载目标commit对象。" << std::endl; return std::nullopt;}
    std::string tree_hash = commit_obj->tree_hash_hex;

    std::filesystem::path head_f = cloned_repo.get_head_file_path(); std::ofstream head_o(head_f, std::ios::trunc);
    if (!head_o) { std::cerr << "错误: 打开HEAD文件失败。" << std::endl; return std::nullopt;}
    if (is_target_a_commit_hash) { head_o << final_commit_to_checkout_hash << std::endl;}
    else {
        std::string local_branch = target_checkout_identifier;
        if (!cloned_repo.branch_exists(local_branch)) {
            if (!cloned_repo.branch_create(local_branch, final_commit_to_checkout_hash)) { std::cerr << "错误: 创建本地分支 '"<<local_branch<<"' 失败。"<<std::endl; head_o.close(); return std::nullopt;}
        }
        head_o << "ref: refs/heads/" << local_branch << std::endl;
    }
    head_o.close(); if(!head_o.good()){ std::cerr << "错误: 写入HEAD文件失败。" << std::endl; return std::nullopt;}

    cloned_repo.index_manager_.clear_in_memory();
    cloned_repo._populate_index_from_tree_recursive(tree_hash, "", cloned_repo.index_manager_);
    if (!cloned_repo.index_manager_.write()) { std::cerr << "错误: 写入初始索引失败。" << std::endl; return std::nullopt;}
    std::map<std::filesystem::path, std::pair<std::string, std::string>> empty_map;
    if (!cloned_repo._update_working_directory_from_tree(tree_hash, empty_map)) { std::cerr << "错误: 更新工作目录失败。" << std::endl; return std::nullopt;}

    if (!is_target_a_commit_hash) {
        std::string local_branch_cfg = target_checkout_identifier;
        cloned_repo.config_set("branch." + local_branch_cfg + ".remote", "origin");
        cloned_repo.config_set("branch." + local_branch_cfg + ".merge", "refs/heads/" + local_branch_cfg);
        std::cout << "  本地分支 '" << local_branch_cfg << "' 已设置以跟踪 'origin/" << local_branch_cfg << "'。" << std::endl;
    }
    std::cout << "  检出完成。" << std::endl;

    // **重要**：根据您的简化方案，clone 时使用的临时 Token (clone_session_token)
    // **不应该**被保存到新克隆的仓库的 .biogit 目录中。
    // 用户需要在新仓库目录下显式执行 `biogit2 login` 来为该仓库获取并存储他们自己的 Token。

    std::cout << "克隆完成。" << std::endl;
    return cloned_repo;
}


/**
 * @brief 私有辅助方法：从给定的起始路径向上查找 .biogit 仓库的工作树根目录。
 * @param starting_path 开始查找的路径，通常是当前工作目录。
 * @return 如果找到，返回仓库工作树的根目录路径 (即包含 .biogit 的目录)；否则返回 std::nullopt。
 */
std::optional<std::filesystem::path> Repository::find_repository_root(const std::filesystem::path &starting_path) {
    std::error_code ec;
    std::filesystem::path current_path = std::filesystem::absolute(starting_path);// 绝对路径

    // 1. 确保起始路径存在且是目录，如果不是，尝试其父目录
    if (!std::filesystem::exists(current_path, ec) || !std::filesystem::is_directory(current_path, ec)) {
        current_path = current_path.parent_path();
    }

    // 向上查找，直到文件系统根目录
    while (current_path.has_parent_path()) { // 避免到达纯粹的根如 "C:" (它没有父路径) 或 "/"
        std::filesystem::path biogit_dir = current_path / MYGIT_DIR_NAME;
        if (std::filesystem::exists(biogit_dir, ec) && std::filesystem::is_directory(biogit_dir, ec)) {
            // 进一步验证是否是合法的 .biogit 目录 (例如检查 HEAD, objects, refs 是否存在)
            // 为简化，这里只检查 .biogit 目录本身
            if (std::filesystem::exists(biogit_dir / HEAD_FILE_NAME, ec) &&
                std::filesystem::exists(biogit_dir / OBJECTS_DIR_NAME, ec) &&
                std::filesystem::exists(biogit_dir / REFS_DIR_NAME, ec)) {
                return current_path; // 返回的是工作树的根目录
            }
        }

        // 如果当前路径已经是根路径的父路径（即空路径），或者与父路径相同，则停止
        if (current_path.parent_path() == current_path || current_path.parent_path().empty()) {
            break;
        }
        current_path = current_path.parent_path();
    }

    // 检查文件系统根目录
    std::filesystem::path biogit_dir_at_root = current_path / MYGIT_DIR_NAME;
    if (std::filesystem::exists(biogit_dir_at_root, ec) && std::filesystem::is_directory(biogit_dir_at_root, ec)) {
        if (std::filesystem::exists(biogit_dir_at_root / HEAD_FILE_NAME, ec) &&
            std::filesystem::exists(biogit_dir_at_root / OBJECTS_DIR_NAME, ec) &&
            std::filesystem::exists(biogit_dir_at_root / REFS_DIR_NAME, ec)) {
            return current_path;
        }
    }

    return std::nullopt; // 未找到
}


/**
 * @brief 把给定路径 转换成 相对于 this->work_tree_root_ 的相对路径 并规范化
 */
std::optional<std::filesystem::path> Repository::normalize_and_relativize_path(
        const std::filesystem::path& user_path) const {

    std::error_code ec;
    std::filesystem::path absolute_user_path;

    if (user_path.is_absolute()) {
        absolute_user_path = user_path;
    } else {
        absolute_user_path = std::filesystem::current_path(ec) / user_path;
        if (ec) {
            std::cerr << "错误: 无法获取当前工作目录: " << ec.message() << std::endl;
            return std::nullopt;
        }
    }
    absolute_user_path = absolute_user_path.lexically_normal();

    std::filesystem::path relative_path = std::filesystem::relative(absolute_user_path, work_tree_root_, ec);

    if (ec) {
        std::cerr << "错误: 计算相对路径失败 ("
                  << absolute_user_path.string() << " 相对于 "
                  << work_tree_root_.string() << "): " << ec.message() << std::endl;
        return std::nullopt;
    }

    if (relative_path.empty() || relative_path.native().find(std::filesystem::path::preferred_separator) == 0 ||
        relative_path.string().rfind("..", 0) == 0) {
        std::cerr << "错误: 文件 '" << user_path.string()
                  << "' (解析为 '" << absolute_user_path.string()
                  << "') 不在工作树 '" << work_tree_root_.string() << "' 内部。" << std::endl;
        return std::nullopt;
        }

    return relative_path.lexically_normal();
}


/**
 * @brief 私有辅助方法：从索引条目构建层级 Tree 对象并返回根 Tree 哈希
 * 注意：index保存的都是相对路径
 */
std::optional<std::string> Repository::_build_trees_and_get_root_hash(
    const std::vector<IndexEntry>& sorted_index_entries) {

    if (sorted_index_entries.empty()) {
        Tree empty_tree;// 如果索引为空，创建一个空的 Tree 对象，保存并返回其哈希
        auto hash_opt = empty_tree.save(get_objects_directory());
        if (!hash_opt) {
            std::cerr << "错误: 保存空 Tree 对象失败。" << std::endl;
        }
        return hash_opt;
    }

    // 1. 收集所有涉及的目录路径 (包括根目录 "")
    std::set<std::filesystem::path> all_dir_paths_set;
    all_dir_paths_set.insert(std::filesystem::path("")); // 根目录

    for (const auto& entry : sorted_index_entries) {
        std::filesystem::path current_parent = entry.file_path.parent_path();
        while (true) {
            all_dir_paths_set.insert(current_parent.lexically_normal());
            if (!current_parent.has_parent_path() || current_parent.parent_path() == current_parent) {
                break; // 到达根或固定点
            }
            current_parent = current_parent.parent_path();
        }
    }

    std::vector<std::filesystem::path> sorted_dir_paths(all_dir_paths_set.begin(), all_dir_paths_set.end());

    // 2. 按深度（路径组件数量）降序排序目录路径，确保子目录先处理
    std::sort(sorted_dir_paths.begin(), sorted_dir_paths.end(),
        [](const std::filesystem::path& a, const std::filesystem::path& b) {
            // 计算路径深度（组件数量）
            auto count_components = [](const std::filesystem::path& p) {
                size_t count = 0;
                for (const auto& component : p) {
                    if (!component.empty() && component.string() != ".") { // 忽略空和"."组件
                        count++;
                    }
                }
                return count;
            };
            size_t depth_a = count_components(a);
            size_t depth_b = count_components(b);

            if (depth_a != depth_b) {
                return depth_a > depth_b; // 深度大的（子目录）在前
            }
            return a.string() > b.string(); // 同深度下按字典序降序，确保一致性
        });

    // 3. 迭代构建和保存 Tree 对象
    std::map<std::filesystem::path, std::string> built_tree_hashes; // 存储已构建的 <目录路径, Tree哈希>

    for (const auto& dir_to_build : sorted_dir_paths) {
        Tree current_dir_tree;

        // a. 添加直接位于此目录下的文件条目
        for (const auto& entry : sorted_index_entries) {
            if (entry.file_path.parent_path().lexically_normal() == dir_to_build.lexically_normal()) {
                current_dir_tree.add_entry(entry.mode,
                                           entry.file_path.filename().string(),
                                           entry.blob_hash_hex);
            }
        }

        // b. 添加直接位于此目录下的子目录（的Tree）条目
        //    遍历所有已构建的树哈希
        for (const auto& pair : built_tree_hashes) {
            const std::filesystem::path& built_subdir_path = pair.first; // 例如 "src", 或者 "src/utils"
            const std::string& subdir_tree_hash = pair.second;

            // 检查 built_subdir_path 是否是 dir_to_build 的直接子目录
            bool is_direct_child = false;
            if (dir_to_build.empty()) { // 如果当前正在构建根目录的 Tree ("")
                // 那么，任何只有一个路径组件的 built_subdir_path 都是它的直接子目录
                // 例如，如果 built_subdir_path 是 "src"，它的迭代器只有一个组件 "src"
                // path("").begin() == path("").end() is true
                // path("src").begin() != path("src").end(), and ++(path("src").begin()) == path("src").end()
                auto it = built_subdir_path.begin();
                if (it != built_subdir_path.end()) { // 确保路径不是空的 (虽然 built_tree_hashes 的键不应该是空的)
                    ++it;
                    if (it == built_subdir_path.end()) { // 只有一个组件
                        is_direct_child = true;
                    }
                }
            } else { // 如果当前正在构建的是一个非根目录的 Tree (例如 "src")
                // 那么，如果 built_subdir_path 的父路径等于 dir_to_build，它就是直接子目录
                if (built_subdir_path.has_parent_path() &&
                    built_subdir_path.parent_path().lexically_normal() == dir_to_build.lexically_normal()) {
                    is_direct_child = true;
                }
            }

            if (is_direct_child) {
                // (加入之前的调试打印，确认是否进入这里)
                // std::cout << "      DEBUG: ==> MATCH! Adding subdir '" << built_subdir_path.filename().string()
                //           << "' to tree for '" << dir_to_build.string() << "'" << std::endl;
                current_dir_tree.add_entry("040000", // 目录模式
                                           built_subdir_path.filename().string(), // 子目录的名称
                                           subdir_tree_hash);
            }
            // else { // (调试打印)
            //      std::cout << "      DEBUG: ==> NO MATCH for subdir '" << built_subdir_path.string()
            //                << "' when building tree for '" << dir_to_build.string() << "'" << std::endl;
            // }
        }
        // Tree::add_entry 内部会排序

        // c. 保存当前构建的 Tree 对象
        auto tree_hash_opt = current_dir_tree.save(get_objects_directory());
        if (!tree_hash_opt) {
            std::cerr << "错误: 保存目录 '" << dir_to_build.string() << "' 的 Tree 对象失败。" << std::endl;
            return std::nullopt;
        }
        built_tree_hashes[dir_to_build] = *tree_hash_opt;
    }

    // 4. 返回根 Tree 的哈希 (对应于空路径 "" 的条目)
    if (built_tree_hashes.count(std::filesystem::path(""))) {
        return built_tree_hashes[std::filesystem::path("")];
    } else {
        // 如果索引为空，上面已处理。如果非空但根树未生成，说明逻辑有误。
        std::cerr << "错误: 未能构建根 Tree 对象 (索引可能非空但无根目录条目被处理)。" << std::endl;
        return std::nullopt;
    }
}


/**
 * @brief 私有辅助方法：获取当前 HEAD 指向的 Commit 哈希
 * @details
 *   HEAD 指向分支 -> 读取对应的分支文件 -> 读取实际的 Commit 哈希 \n
 *   HEAD 分离头 ->  则返回该哈希 \n
 *   如果无法确定（例如新仓库，文件不存在或格式错误），返回 std::nullopt
 */
std::optional<std::string> Repository::_get_head_commit_hash() const {
    std::filesystem::path head_path = get_head_file_path(); // 获取 HEAD 文件路径
    if (!std::filesystem::exists(head_path)) {
        return std::nullopt; // HEAD 文件不存在，通常在新仓库的第一次提交前
    }

    std::ifstream head_file(head_path);
    std::string line;
    if (std::getline(head_file, line)) { // 读取 HEAD 文件内容
        // 清理可能的行尾换行符
        if (!line.empty() && (line.back() == '\n' || line.back() == '\r')) { line.pop_back(); }
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }

        // 检查是否是符号引用，格式如 "ref: refs/heads/main"
        if (line.rfind("ref: ", 0) == 0 && line.length() > 5) {
            std::string ref_path_str = line.substr(5); // 提取引用路径，例如 "refs/heads/main"
            ref_path_str.erase(0, ref_path_str.find_first_not_of(" ")); // 去除前导空格

            std::filesystem::path branch_file_path = mygit_dir_ / ref_path_str; // 构建分支文件的完整路径
            if (std::filesystem::exists(branch_file_path)) {
                std::ifstream branch_file(branch_file_path);
                std::string commit_hash;
                // 从分支文件中读取 Commit 哈希
                if (branch_file >> commit_hash && commit_hash.length() == 40) {
                    return commit_hash; // 返回分支指向的 Commit 哈希
                }
            }
            // 如果分支文件不存在或内容无效，则认为该分支尚无提交
        } else if (line.length() == 40) { // 如果内容是40字符，认为是分离头指针状态
            return line; // 直接返回 Commit 哈希
        }
    }
    return std::nullopt; // 无法确定 HEAD Commit 哈希
}


/**
 * @brief 私有辅助方法：递归加载 Tree 内容到 Map
 * 将指定 Tree 对象及其所有子 Tree 中的文件（Blob）条目，以 <相对路径, {Blob哈希, 模式}> 的形式存入 files_map
 * @param tree_hash_hex : 当前要加载的 Tree 对象的哈希 (十六进制字符串)
 * @param current_path_prefix : 用于构建文件在仓库中完整相对路径的前缀。
 * @param files_map: 用于存储结果的引用
 */
void Repository::_load_tree_contents_recursive(const std::string &tree_hash_hex,
    const std::filesystem::path &current_path_prefix,
    std::map<std::filesystem::path, std::pair<std::string, std::string>> &files_map) const {

    // 从对象库加载 Tree 对象
    auto tree_opt = Tree::load_by_hash(tree_hash_hex, get_objects_directory());
    if (!tree_opt) {
        std::cerr << "警告 (status): 无法加载 Tree 对象 " << tree_hash_hex << std::endl;
        return; // 加载失败则中止此路径的递归
    }

    // 遍历 Tree 对象中的每一个条目 (TreeEntry)
    for (const auto& entry : tree_opt->entries) {
        // 构建当前条目在仓库中的完整相对路径
        std::filesystem::path entry_full_path = current_path_prefix / entry.name;
        entry_full_path = entry_full_path.lexically_normal(); // 规范化路径

        if (entry.is_directory()) { // 如果条目是一个目录 (模式 "040000")
            // 递归调用自身，加载子目录的 Tree 对象
            // entry.sha1_hash_hex 已经是十六进制字符串 (根据我们之前的Tree实现)
            _load_tree_contents_recursive(entry.sha1_hash_hex, entry_full_path, files_map);
        } else { // 如果条目是一个文件 (Blob)
            // 将文件的相对路径、Blob哈希和模式存入结果 map
            files_map[entry_full_path] = {entry.sha1_hash_hex, entry.mode};
        }
    }
}


/**
 * @brief 私有辅助方法：从给定的 Tree 哈希递归填充 Index 对象
 * @param tree_hash_hex : 当前要加载的 Tree 对象的哈希 (十六进制字符串)
 * @param current_path_prefix : 用于构建文件在仓库中完整相对路径的前缀。
 * @param target_index // 传递 Index 对象的引用以直接修改
 * @detail
 *     Tree 格式 <模式> <名称>\0<哈希>
 *     Index 格式 <模式> <Blob哈希> <mtime秒> <mtime纳秒> <文件大小> <文件路径(相对于工作树根目录)>
 */
void Repository::_populate_index_from_tree_recursive(
    const std::string& tree_hash_hex,
    const std::filesystem::path& current_path_prefix,
    Index& target_index // 注意：传入的是 Index 对象的引用
) const {
    auto tree_opt = Tree::load_by_hash(tree_hash_hex, get_objects_directory());
    if (!tree_opt) {
        std::cerr << "警告 (populate_index): 无法加载 Tree 对象 " << tree_hash_hex
                  << " 当为路径 '" << current_path_prefix.string() << "' 填充索引时。" << std::endl;
        return;
    }

    for (const auto& entry : tree_opt->entries) {
        std::filesystem::path entry_full_relative_path = (current_path_prefix / entry.name).lexically_normal();

        if (entry.is_directory()) { // 模式 "040000"
            _populate_index_from_tree_recursive(entry.sha1_hash_hex, entry_full_relative_path, target_index);
        } else { // 是文件 (Blob)
            // Tree 对象本身不存储元数据 需要读取文件
            std::filesystem::path abs_file_path_in_worktree = work_tree_root_ / entry_full_relative_path;
            std::error_code ec;
            std::chrono::system_clock::time_point mtime;
            uint64_t file_size = 0;

            if (std::filesystem::exists(abs_file_path_in_worktree, ec) &&
                std::filesystem::is_regular_file(abs_file_path_in_worktree, ec)) {

                auto ftime = std::filesystem::last_write_time(abs_file_path_in_worktree, ec);
                if (!ec) {
                    // C++17 时间转换
                    auto file_time_duration_since_epoch = ftime.time_since_epoch();
                    auto system_clock_compatible_duration =
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(file_time_duration_since_epoch);
                    mtime = std::chrono::system_clock::time_point(system_clock_compatible_duration);
                } else {
                     std::cerr << "警告 (populate_index): 无法获取文件 '" << abs_file_path_in_worktree.string()
                               << "' 的mtime，将使用当前时间。" << std::endl;
                     mtime = std::chrono::system_clock::now(); // 或者一个默认值
                }

                file_size = std::filesystem::file_size(abs_file_path_in_worktree, ec);
                if (ec) {
                    std::cerr << "警告 (populate_index): 无法获取文件 '" << abs_file_path_in_worktree.string()
                              << "' 的size，将使用0。" << std::endl;
                    file_size = 0; // 或者一个默认值
                }
            } else {
                 //std::cerr << "警告 (populate_index): 文件 '" << abs_file_path_in_worktree.string()<< "' 在工作目录中未找到或不是常规文件，无法获取元数据。" << std::endl;
                 // 对于这种情况，mtime 和 file_size 将是默认的或之前设置的值
                 mtime = std::chrono::system_clock::now(); // 或其他默认
                 file_size = 0; // 或其他默认
            }

            // 调用 Index 的 add_or_update_entry 方法
            // 注意：entry.mode 和 entry.sha1_hash_hex 来自于刚提交的 Tree 对象
            target_index.add_or_update_entry(
                entry_full_relative_path,
                entry.sha1_hash_hex,
                entry.mode,
                mtime,
                file_size
            );
        }
    }
}


/**
 * @brief 私有辅助方法：拆解三种对象头部和内容
 * @param file_path :
 * @return tuple< 对象类型 , 内容长度 , 二进制内容 >
 */
std::optional<std::tuple<std::string, size_t, std::vector<std::byte>>> Repository::read_and_parse_object_file_content (
    const std::filesystem::path &file_path) const {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "错误 (read_object): 无法打开对象文件 '" << file_path.string() << "'" << std::endl;
        return std::nullopt;
    }
    std::string type_str_read;
    long long content_size_read = -1;
    char ch;
    while (ifs.get(ch) && ch != ' ') { type_str_read += ch; }
    if (type_str_read.empty() || ch != ' ') { return std::nullopt; }
    std::string size_str_read;
    while (ifs.get(ch) && ch != '\0') { size_str_read += ch; }
    if (size_str_read.empty() || ch != '\0') { return std::nullopt; }
    try { content_size_read = std::stoll(size_str_read); }
    catch (const std::exception&) { return std::nullopt; }
    if (content_size_read < 0) { return std::nullopt; }
    std::vector<std::byte> content_data(static_cast<size_t>(content_size_read));
    if (content_size_read > 0) {
        ifs.read(reinterpret_cast<char*>(content_data.data()), content_size_read);
        if (ifs.gcount() != content_size_read) { return std::nullopt; }
    }
    return std::make_tuple(type_str_read, static_cast<size_t>(content_size_read), content_data);
}


/**
 * @brief 私有辅助方法：通过哈希前缀查找唯一的对象文件路径
 * @param hash_prefix : 哈希前缀
 * @return 存在唯一对象 返回路径 否则 std::nullopt
 */
std::optional<std::filesystem::path> Repository::_find_object_file_by_prefix(const std::string& hash_prefix) const {
    if (hash_prefix.length() < 6) { // 最小短哈希长度为6
        std::cerr << "错误: 哈希前缀太短，至少需要6个字符。" << std::endl;
        return std::nullopt;
    }

    std::string subdir_name = hash_prefix.substr(0, 2);
    std::string remaining_prefix = hash_prefix.substr(2);

    std::filesystem::path object_subdir = get_objects_directory() / subdir_name;
    std::error_code ec;

    if (!std::filesystem::exists(object_subdir, ec) || !std::filesystem::is_directory(object_subdir, ec)) {
        //std::cerr << "提示: 未找到对象目录 " << object_subdir.string() << " (对于哈希前缀 " << hash_prefix << ")" << std::endl;
        return std::nullopt;
    }
    if(ec) {
        std::cerr << "错误: 检查对象目录 " << object_subdir.string() << " 时出错: " << ec.message() << std::endl;
        return std::nullopt;
    }


    std::vector<std::filesystem::path> matches;
    // 遍历对象目录所有文件 对比文件名字
    for (const auto& entry : std::filesystem::directory_iterator(object_subdir, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string filename = entry.path().filename().string();
            if (filename.rfind(remaining_prefix, 0) == 0) {
                matches.push_back(entry.path());
            }
        } else if (ec) {
            std::cerr << "警告: 遍历对象目录 " << object_subdir.string() << " 时出错: " << entry.path().string() << ": " << ec.message() << std::endl;
            ec.clear(); // 清除错误码继续
        }
    }
    if (ec) { // directory_iterator 构造或迭代中的错误
        std::cerr << "错误: 遍历对象目录 " << object_subdir.string() << " 失败: " << ec.message() << std::endl;
        return std::nullopt;
    }


    if (matches.empty()) {
        // std::cerr << "提示: 未找到以 '" << hash_prefix << "' 开头的对象。" << std::endl;
        return std::nullopt;
    } else if (matches.size() > 1) {
        std::cerr << "错误: 短哈希 '" << hash_prefix << "' 具有歧义，匹配到多个对象:" << std::endl;
        for (const auto& match_path : matches) {
            std::cerr << "  " << subdir_name << match_path.filename().string() << std::endl;
        }
        return std::nullopt;
    }

    // 只找到一个匹配
    return matches[0];
}


/**
 * @brief 私有辅助方法：处理分支名、"HEAD"、短哈希和长哈希 尽可能完整commit哈希
 * @param name_or_hash_prefix : 分支名、"HEAD"、短哈希和长哈希 尽可能完整commit哈希、标签（未来）
 */
std::optional<std::string> Repository::_resolve_commit_ish_to_full_hash(const std::string& name_or_hash_prefix) const {
    if (name_or_hash_prefix.empty()) {
        std::cerr << "调试: 输入的名称或哈希前缀为空。" << std::endl; // 可以作为调试信息
        return std::nullopt;
    }

    // 优先顺序：
    // 1. "HEAD" 关键字
    // 2. 完整的引用路径 (refs/heads/..., refs/tags/..., refs/remotes/...)
    // 3. 远程跟踪分支的简写 (origin/main)
    // 4. 短的分支名
    // 5. 短的标签名
    // 6. 哈希或哈希前缀

    // 1. 检查是否是 "HEAD" 关键字
    if (name_or_hash_prefix == "HEAD") {
        return _get_head_commit_hash(); // _get_head_commit_hash 应该在 Repository.cpp 中定义
    }

    // 2. 检查是否是完整的引用路径 (以 "refs/" 开头)
    if (name_or_hash_prefix.rfind("refs/", 0) == 0) {
        std::filesystem::path ref_file_path = mygit_dir_ / name_or_hash_prefix;
        if (std::filesystem::exists(ref_file_path) && std::filesystem::is_regular_file(ref_file_path)) {
            std::ifstream ref_file(ref_file_path);
            std::string commit_hash_str;
            if (ref_file >> commit_hash_str && commit_hash_str.length() == 40) {
                // 验证这个哈希确实是一个 commit 对象
                auto commit_obj_opt = Commit::load_by_hash(commit_hash_str, get_objects_directory());
                if (commit_obj_opt) {
                    return commit_hash_str;
                } else {
                    // std::cerr << "调试: '" << name_or_hash_prefix << "' 指向的哈希 " << commit_hash_str.substr(0,7) << " 不是一个有效的 commit 对象。" << std::endl;
                }
            }
        }else{
            return std::nullopt; // 强制 refs/ 开头必须是是引用类型
        }
    }

    // 3. 尝试解析 <remote>/<branch> 格式的远程跟踪分支的简写 例如："origin/main" -> ".biogit/refs/remotes/origin/main"
    size_t first_slash_pos = name_or_hash_prefix.find('/');
    // 确保只有一个斜杠，并且它不在字符串的开头或末尾
    if (first_slash_pos != std::string::npos &&
        name_or_hash_prefix.find('/', first_slash_pos + 1) == std::string::npos &&
        first_slash_pos != 0 && first_slash_pos != name_or_hash_prefix.length() - 1) {

        std::string remote_name_part = name_or_hash_prefix.substr(0, first_slash_pos);
        std::string branch_name_part = name_or_hash_prefix.substr(first_slash_pos + 1);

        std::filesystem::path remote_tracking_branch_file_path =
            mygit_dir_ / "refs" / "remotes" / remote_name_part / branch_name_part;

        if (std::filesystem::exists(remote_tracking_branch_file_path) &&
            std::filesystem::is_regular_file(remote_tracking_branch_file_path)) {
            std::ifstream ref_file(remote_tracking_branch_file_path);
            std::string commit_hash_str;
            if (ref_file >> commit_hash_str && commit_hash_str.length() == 40) {
                auto commit_obj_opt = Commit::load_by_hash(commit_hash_str, get_objects_directory());
                if (commit_obj_opt) {
                    return commit_hash_str;
                }
            }
        }else {
            return std::nullopt;// 不允许存在 / 的短 引用名字
        }
    }

    // 4. 检查是否是短的分支名 (例如 "main" -> ".biogit/refs/heads/main")
    //    只在输入不包含 '/' 时才尝试，以避免与远程跟踪分支的简写形式冲突。
    if (name_or_hash_prefix.find('/') == std::string::npos) {
        std::filesystem::path branch_file_path = get_heads_directory() / name_or_hash_prefix; //
        if (std::filesystem::exists(branch_file_path) && std::filesystem::is_regular_file(branch_file_path)) {
            std::ifstream branch_file(branch_file_path);
            std::string commit_hash_str;
            if (branch_file >> commit_hash_str && commit_hash_str.length() == 40) {
                auto commit_obj_opt = Commit::load_by_hash(commit_hash_str, get_objects_directory());
                if (commit_obj_opt) {
                    return commit_hash_str;
                }
            }
        }
    }

    // 5. 检查是否是短的标签名 (例如 "v1.0" -> ".biogit/refs/tags/v1.0")
    //    同样，只在输入不包含 '/' 时才尝试。
    if (name_or_hash_prefix.find('/') == std::string::npos) {
        std::filesystem::path tag_file_path = get_tags_directory() / name_or_hash_prefix; //
        if (std::filesystem::exists(tag_file_path) && std::filesystem::is_regular_file(tag_file_path)) {
            std::ifstream tag_file(tag_file_path);
            std::string target_hash_str; // 标签可能指向 commit 或另一个 tag 对象 (附注标签)
            if (tag_file >> target_hash_str && target_hash_str.length() == 40) {
                // 当前假设是轻量标签，直接指向 commit
                auto commit_obj_opt = Commit::load_by_hash(target_hash_str, get_objects_directory());
                if (commit_obj_opt) {
                    return target_hash_str;
                }
                // TODO: 如果要支持附注标签，这里需要检查 target_hash_str 是否是一个 tag 对象，然后加载该 tag 对象并获取其指向的 commit 哈希。
            }
        }
    }

    // 6. 尝试作为 commit 哈希或哈希前缀 (至少6个字符)
    //    确保输入看起来像一个哈希 (全是十六进制字符)
    if (name_or_hash_prefix.length() >= 6 && name_or_hash_prefix.length() <= 40 &&
        std::all_of(name_or_hash_prefix.begin(), name_or_hash_prefix.end(), ::isxdigit)) {

        std::optional<std::filesystem::path> object_file_path_opt = _find_object_file_by_prefix(name_or_hash_prefix); //
        if (object_file_path_opt) {
            // 从找到的文件路径中提取完整的40位哈希
            std::string dir_part_str = object_file_path_opt->parent_path().filename().string();
            std::string file_part_str = object_file_path_opt->filename().string();
            std::string full_hash_str = dir_part_str + file_part_str;

            if (full_hash_str.length() == 40) {
                // 验证这个哈希确实是一个 commit 对象
                auto commit_obj_opt = Commit::load_by_hash(full_hash_str, get_objects_directory());
                if (commit_obj_opt) {
                    return full_hash_str;
                } else {
                    // std::cerr << "调试: 哈希 " << full_hash_str.substr(0,7) << " 找到了对象文件，但它不是一个 commit 对象。" << std::endl;
                }
            }
        }
    }

    // 如果所有尝试都失败
    std::cerr << "错误: 无法将 '" << name_or_hash_prefix << "' 解析为一个有效的 commit。" << std::endl;
    return std::nullopt;
}


/**
 * @brief 根据给定的 SHA-1 哈希 (至少6个字符) 加载并显示对象内容。
 * 支持 Blob, Tree, 和 Commit 对象。
 * @param object_hash_prefix 要显示对象的SHA-1哈希前缀。
 * @param pretty_print (可选) 是否以更美观的格式打印。
 * @return 如果成功加载并显示对象，返回 true；如果找不到对象、哈希有歧义或发生错误，返回 false。
 */
bool Repository::show_object_by_hash(const std::string& object_hash_prefix, bool pretty_print)  {
    // 1. 通过哈希前缀找到唯一的对象文件路径
    std::optional<std::filesystem::path> object_file_path_opt = _find_object_file_by_prefix(object_hash_prefix);

    if (!object_file_path_opt) {
        std::cout << "错误: 未找到对象或哈希前缀 '" << object_hash_prefix << "' 具有歧义。" << std::endl;
        return false;
    }
    std::filesystem::path object_file_path = *object_file_path_opt;

    // 2. 读取并解析对象文件的头部和内容
    auto parsed_result = read_and_parse_object_file_content(object_file_path);
    if (!parsed_result) {
        std::cerr << "错误: 无法读取或解析对象文件: " << object_file_path.string() << std::endl;
        return false;
    }

    const auto& [type_str_read, content_size_read, raw_content_data] = *parsed_result;

    // 3. 根据对象类型，反序列化并打印内容
    if (pretty_print) { // “美化”打印，类似 git cat-file -p
        if (type_str_read == Blob::type_str()) {
            // Blob 对象的内容就是原始文件内容，直接打印
            for (std::byte b : raw_content_data) {
                std::cout << static_cast<char>(b);
            }
            // 通常会确保末尾有换行，如果原始数据没有
            if (!raw_content_data.empty() && raw_content_data.back() != static_cast<std::byte>('\n')) {
                std::cout << std::endl;
            }
        } else if (type_str_read == Tree::type_str()) {
            auto tree_opt = Tree::deserialize(raw_content_data);
            if (tree_opt) {
                // Tree 对象的条目应该是已排序的 (在其序列化/反序列化逻辑中保证)
                for (const auto& entry : tree_opt->entries) {
                    std::cout << std::setw(6) << std::left << entry.mode << " "
                              << (entry.is_directory() ? Tree::type_str() : Blob::type_str()) << " "
                              << entry.sha1_hash_hex << "\t" // 使用 entry 中已有的十六进制哈希
                              << entry.name << std::endl;
                }
            } else {
                std::cerr << "错误: 反序列化 Tree 对象失败: " << object_file_path.string() << std::endl;
                return false;
            }
        } else if (type_str_read == Commit::type_str()) {
            auto commit_opt = Commit::deserialize(raw_content_data);
            if (commit_opt) {
                const Commit& commit = *commit_opt;
                std::cout << "tree " << commit.tree_hash_hex << std::endl;
                for (const auto& parent_hash : commit.parent_hashes_hex) {
                    std::cout << "parent " << parent_hash << std::endl;
                }
                std::cout << "author " << commit.author.format_for_commit() << std::endl;
                std::cout << "committer " << commit.committer.format_for_commit() << std::endl;
                std::cout << std::endl; // 空行
                std::cout << commit.message << std::endl;
                // 确保提交信息末尾有换行（如果它本身没有）
                if (!commit.message.empty() && commit.message.back() != '\n') {
                    std::cout << std::endl;
                }
            } else {
                std::cerr << "错误: 反序列化 Commit 对象失败: " << object_file_path.string() << std::endl;
                return false;
            }
        } else {
            std::cerr << "错误: 未知的对象类型 '" << type_str_read << "' 于文件 " << object_file_path.string() << std::endl;
            return false;
        }
    } else {
        // 只打印原始内容
        for (std::byte b : raw_content_data) {
            std::cout << static_cast<char>(b);
        }
        // 确保换行
        if (!raw_content_data.empty() && raw_content_data.back() != static_cast<std::byte>('\n')) {
             std::cout << std::endl;
        }
    }

    return true;
}


bool Repository::is_workspace_clean() const {
    // 1. 加载 HEAD Commit 的文件树
    std::map<std::filesystem::path, std::pair<std::string, std::string>> head_files_map;
    std::optional<std::string> head_commit_hash_opt = _get_head_commit_hash();
    if (head_commit_hash_opt) {
        auto commit_opt = Commit::load_by_hash(*head_commit_hash_opt, get_objects_directory());
        if (commit_opt) {
            _load_tree_contents_recursive(commit_opt->tree_hash_hex, "", head_files_map);
        }
    }
    // 如果 head_commit_hash_opt 为空（新仓库），则 head_files_map 为空

    // 2. 加载 Index 内容
    Index index_reader(mygit_dir_ );
    if (!index_reader.load() && std::filesystem::exists(mygit_dir_ / INDEX_FILE_NAME)) {
        std::cerr << "错误 (is_workspace_clean): 加载索引失败。" << std::endl;
        return false; // 无法确定状态，保守处理为不干净
    }
    const auto& staged_entries = index_reader.get_all_entries();
    std::map<std::filesystem::path, const IndexEntry*> staged_files_map;
    for (const auto& entry : staged_entries) {
        staged_files_map[entry.file_path] = &entry;
    }

    // 3. 比较 Index vs HEAD (检查是否有“要提交的更改”)
    for (const auto& staged_pair : staged_files_map) {
        const auto& rel_path = staged_pair.first;
        const auto* staged_entry = staged_pair.second;
        auto head_it = head_files_map.find(rel_path);
        if (head_it == head_files_map.end()) { // 文件在 Index 中，但不在 HEAD 中 (新暂存的文件)
            std::cout << "  提示 (is_workspace_clean): 已暂存的新文件: " << rel_path.string() << std::endl;
            return false;
        }
        if (staged_entry->blob_hash_hex != head_it->second.first || staged_entry->mode != head_it->second.second) {
            std::cout << "  提示 (is_workspace_clean): 已暂存的修改: " << rel_path.string() << std::endl;
            return false;
        }
    }
    for (const auto& head_pair : head_files_map) {
        if (staged_files_map.find(head_pair.first) == staged_files_map.end()) {
            std::cout << "  提示 (is_workspace_clean): 已暂存的删除: " << head_pair.first.string() << std::endl;
            return false;
        }
    }

    // 4. 比较 Working Directory vs Index (检查是否有“未暂存的更改”)
    std::error_code ec_wd;
    if (std::filesystem::exists(work_tree_root_) && std::filesystem::is_directory(work_tree_root_)) {
        std::filesystem::recursive_directory_iterator dir_iter(
            work_tree_root_,
            std::filesystem::directory_options::skip_permission_denied,
            ec_wd
        );
        if (ec_wd) {
            std::cerr << "错误 (is_workspace_clean): 无法遍历工作目录: " << ec_wd.message() << std::endl;
            return false; // 无法确定，保守处理
        }

        std::filesystem::recursive_directory_iterator end_iter;
        for (; dir_iter != end_iter; ++dir_iter) {
            std::error_code entry_ec;
            const auto& current_dir_entry_obj = *dir_iter;
            const auto& current_abs_path_from_iterator = current_dir_entry_obj.path();

            // 跳过 .biogit 目录
            if (current_abs_path_from_iterator.lexically_normal().string().rfind(mygit_dir_.lexically_normal().string(), 0) == 0) {
                if (dir_iter.depth() == 0 && current_abs_path_from_iterator.filename() == MYGIT_DIR_NAME) {
                     dir_iter.disable_recursion_pending();
                }
                continue;
            }

            if (current_dir_entry_obj.is_regular_file(entry_ec)) {
                std::filesystem::path rel_path = std::filesystem::relative(current_abs_path_from_iterator.lexically_normal(), work_tree_root_, entry_ec).lexically_normal();
                if (entry_ec || rel_path.empty() || rel_path.string().rfind("..",0) == 0) continue;

                auto staged_it = staged_files_map.find(rel_path);
                if (staged_it != staged_files_map.end()) { // 文件被跟踪
                    const IndexEntry* staged_entry = staged_it->second;
                    // bool metadata_differs = false;
                    // auto ftime_workdir = std::filesystem::last_write_time(current_abs_path_from_iterator, entry_ec);
                    // if (entry_ec) { metadata_differs = true; }
                    // else {
                    //     auto file_time_duration_since_epoch = ftime_workdir.time_since_epoch();
                    //     auto system_clock_compatible_duration =
                    //         std::chrono::duration_cast<std::chrono::system_clock::duration>(file_time_duration_since_epoch);
                    //     std::chrono::system_clock::time_point mtime_workdir(system_clock_compatible_duration);
                    //     if (mtime_workdir != staged_entry->mtime) metadata_differs = true;
                    // }
                    // uintmax_t size_workdir = std::filesystem::file_size(current_abs_path_from_iterator, entry_ec);
                    // if (entry_ec) { metadata_differs = true; }
                    // else if (size_workdir != staged_entry->file_size) metadata_differs = true;
                    //
                    // if (metadata_differs) 元数据不同，需比较内容
                        std::ifstream ifs_wd(current_abs_path_from_iterator, std::ios::binary);
                        std::vector<std::byte> content_wd;
                        if(ifs_wd.is_open()){
                            ifs_wd.seekg(0, std::ios::end); std::streamsize f_size = ifs_wd.tellg(); ifs_wd.seekg(0, std::ios::beg);
                            if(f_size > 0) { content_wd.resize(f_size); ifs_wd.read(reinterpret_cast<char*>(content_wd.data()), f_size); }
                            ifs_wd.close();
                        }
                        Blob temp_blob_wd(content_wd);
                        if (SHA1::sha1(temp_blob_wd.serialize()) != staged_entry->blob_hash_hex) {
                            std::cout << "  提示 (is_workspace_clean): 工作区修改未暂存: " << rel_path.string() << std::endl;
                            return false; // 内容已修改但未暂存
                        }

                }
                // 未跟踪文件不影响“干净”状态的判断 (对于是否允许切换而言)
            }
        }
    }
    // 检查是否有在索引中但工作目录中被删除的文件
    for(const auto& staged_pair : staged_files_map) {
        if (!std::filesystem::exists(work_tree_root_ / staged_pair.first))  {
            // 文件在索引中，但在工作目录遍历时未找到
            std::cout << "  提示 (is_workspace_clean): 文件从工作区删除但未暂存: " << staged_pair.first.string() << std::endl;
            return false;
        }
    }

    return true; // 如果所有检查通过，则工作区是干净的
}


/**
 * @brief 根据目标Tree的内容更新工作目录。
 * @param target_root_tree_hash 目标Commit的根Tree对象的哈希。
 * @param current_head_files_map (可选) 当前HEAD的文件列表，用于比较和删除。
 * @return 如果成功更新工作目录，返回 true；否则返回 false。
 */
bool Repository::_update_working_directory_from_tree(
    const std::string& target_root_tree_hash,
    const std::map<std::filesystem::path, std::pair<std::string, std::string>>& current_head_files_map) {

    std::map<std::filesystem::path, std::pair<std::string, std::string>> target_tree_files_map;
    _load_tree_contents_recursive(target_root_tree_hash, "", target_tree_files_map);

    std::error_code ec;

    // 1. 删除/修改工作目录中存在于 current_head 但不在 target_tree 中的文件，
    //    或者内容/模式有变化的文件。
    std::set<std::filesystem::path> paths_in_target_tree;
    for (const auto& target_pair : target_tree_files_map) {
        paths_in_target_tree.insert(target_pair.first);
        const std::filesystem::path& rel_path = target_pair.first;
        const std::string& target_blob_hash = target_pair.second.first;
        // const std::string& target_mode = target_pair.second.second; // 模式用于创建文件，这里主要用blob

        std::filesystem::path abs_path_in_worktree = work_tree_root_ / rel_path;

        auto current_head_it = current_head_files_map.find(rel_path);
        bool needs_update = true;
        if (current_head_it != current_head_files_map.end()) {
            if (current_head_it->second.first == target_blob_hash) { // 假设模式也相同，或简化不比较模式
                needs_update = false; // 内容相同，无需更新
            }
        }
        // (更精确的：如果文件已在工作区且元数据与index一致，可跳过写，但index刚被target_tree重置)
        // 简单起见，只要是目标树中的文件，就确保其内容正确。

        if (needs_update || !std::filesystem::exists(abs_path_in_worktree)) {
            // 从对象库加载 Blob 内容
            auto blob_opt = Blob::load_by_hash(target_blob_hash, get_objects_directory());
            if (!blob_opt) {
                std::cerr << "错误 (checkout): 无法加载 Blob " << target_blob_hash << " 用于文件 " << rel_path.string() << std::endl;
                return false; // 关键Blob丢失，更新失败
            }
            // 创建父目录 (如果不存在)
            if (abs_path_in_worktree.has_parent_path()) {
                std::filesystem::create_directories(abs_path_in_worktree.parent_path(), ec);
                if (ec) {
                     std::cerr << "错误 (checkout): 创建目录 " << abs_path_in_worktree.parent_path().string() << " 失败: " << ec.message() << std::endl;
                     return false;
                }
            }
            // 写入文件
            std::ofstream ofs(abs_path_in_worktree, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                std::cerr << "错误 (checkout): 无法写入文件 " << abs_path_in_worktree.string() << std::endl;
                return false;
            }
            const auto& content_bytes = blob_opt->content;
            if (!content_bytes.empty()) {
                ofs.write(reinterpret_cast<const char*>(content_bytes.data()), content_bytes.size());
            }
            ofs.close();
            if (!ofs.good()){
                 std::cerr << "错误 (checkout): 写入文件 " << abs_path_in_worktree.string() << " 时发生错误。" << std::endl;
                 return false;
            }
            // TODO: 设置文件模式 (例如可执行位)，简化版中可省略
        }
    }

    // 2. 删除工作目录中存在于 current_head (或只是存在于磁盘) 但不在 target_tree 中的文件
    //    我们需要遍历实际工作目录的文件，或者依赖 current_head_files_map
    //    更安全的方式是，遍历 current_head_files_map，如果文件不在 paths_in_target_tree 中，则删除。
    //    然后，还需要处理那些在工作目录中，但既不在 current_head 也不在 target_tree 中的文件（这些不应该存在，因为我们要求工作区干净）
    //    为了简化，我们只处理从 current_head 到 target_tree 的过渡。

    for (const auto& head_file_pair : current_head_files_map) {
        const std::filesystem::path& rel_path = head_file_pair.first;
        if (paths_in_target_tree.find(rel_path) == paths_in_target_tree.end()) {
            // 这个文件在旧的HEAD中，但在新的目标树中没有，需要从工作目录删除
            std::filesystem::path abs_path_to_delete = work_tree_root_ / rel_path;
            if (std::filesystem::exists(abs_path_to_delete, ec)) {
                std::filesystem::remove(abs_path_to_delete, ec);
                if (ec) {
                    std::cerr << "警告 (checkout): 删除旧文件 " << abs_path_to_delete.string() << " 失败: " << ec.message() << std::endl;
                    // 不一定是致命错误，但记录下来
                }
            }
        }
    }
    // TODO: 递归删除空目录 (可选)

    return true;
}


/**
 * @brief 获取指定 Blob 哈希对应的内容，并按行分割。
 * @param blob_hash 要加载的 Blob 的哈希。
 * @return 如果成功，返回行列表；否则返回 std::nullopt。
 */
std::optional<std::vector<std::string>> Repository::_get_blob_lines(const std::string& blob_hash) const {
    auto blob_opt = Blob::load_by_hash(blob_hash, get_objects_directory());
    if (blob_opt) {
        return Utils::string_to_lines(blob_opt->get_content_as_string());
    }
    std::cerr << "错误: 无法加载 Blob 对象 " << blob_hash << std::endl;
    return std::nullopt;
}


/**
 * @brief 获取工作目录中指定相对路径文件的内容，并按行分割。
 * @param relative_path 文件相对于工作树根的路径。
 * @return 如果成功，返回行列表；否则返回 std::nullopt。
 */
std::optional<std::vector<std::string>> Repository::_get_workdir_lines(const std::filesystem::path& relative_path) const {
    std::filesystem::path absolute_path = work_tree_root_ / relative_path;
    if (!std::filesystem::exists(absolute_path) || !std::filesystem::is_regular_file(absolute_path)) {
        return std::nullopt; // 文件不存在或不是常规文件
    }

    std::ifstream ifs(absolute_path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "错误: 无法打开工作目录文件 '" << absolute_path.string() << "'。" << std::endl;
        return std::nullopt;
    }

    // 将文件内容读入字符串
    std::string content_str((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
    ifs.close();
    return Utils::string_to_lines(content_str);
}


/**
 * @brief 比较并打印两个版本文件内容的差异。
 * @param display_path 用于在 diff 输出中显示的文件路径。
 * @param lines_a “旧”版本文件的行。
 * @param label_a_suffix 旧版本文件的标签后缀 (例如 " (HEAD)", " (Index)")。
 * @param lines_b “新”版本文件的行。
 * @param label_b_suffix 新版本文件的标签后缀 (例如 " (Index)", " (Working Directory)")。
 */
void Repository::_perform_and_print_file_diff(
        const std::filesystem::path& display_path,
        const std::vector<std::string>& lines_a,
        const std::string& label_a_suffix,
        const std::vector<std::string>& lines_b,
        const std::string& label_b_suffix){


    std::vector<Utils::LineEditOperation> ses = Utils::MyersDiffLines(lines_a, lines_b);

    // 调用格式化打印函数
    // 注意：print_unified_diff 需要能接收并使用 label_a_suffix 和 label_b_suffix
    // 例如，在其打印 "--- a/..." 和 "+++ b/..." 时附加上这些后缀。
    // 确保您的 print_unified_diff 实现支持这个。
    // 为了简洁，我假设您的 print_unified_diff 内部会处理这个。
    // 如果不是，您可能需要在这里构建完整的标签字符串传递给它。
    print_unified_diff(display_path, ses, label_a_suffix, label_b_suffix);
}


/**
 * @brief 查找两个 commit 之间的最近共同祖先（LCA - Lowest Common Ancestor）。
 * @param commit_hash1 第一个 commit 的哈希。
 * @param commit_hash2 第二个 commit 的哈希。
 * @return 如果找到，返回共同祖先的 commit 哈希；否则返回 std::nullopt。
 */
std::optional<std::string> Repository::_find_common_ancestor(
        const std::string& commit_hash1,
        const std::string& commit_hash2) const {

    if (commit_hash1.empty() || commit_hash2.empty()) {
        return std::nullopt;
    }
    if (commit_hash1 == commit_hash2) {
        return commit_hash1;
    }

    // 1. 获取 commit_hash1 的所有祖先 (包括它自己)
    std::set<std::string> ancestors1;
    std::queue<std::string> q1;
    std::set<std::string> visited1_bfs; // 用于 BFS 遍历时防止重复访问和循环

    q1.push(commit_hash1);
    visited1_bfs.insert(commit_hash1);

    while (!q1.empty()) {
        std::string current_c1 = q1.front();
        q1.pop();
        ancestors1.insert(current_c1);

        auto commit1_obj_opt = Commit::load_by_hash(current_c1, get_objects_directory()); //
        if (commit1_obj_opt) {
            for (const auto& parent_hash : commit1_obj_opt->parent_hashes_hex) { //
                if (visited1_bfs.find(parent_hash) == visited1_bfs.end()) {
                    visited1_bfs.insert(parent_hash);
                    q1.push(parent_hash);
                }
            }
        }
    }

    // 2. 从 commit_hash2 开始，通过 BFS 向上查找第一个也存在于 ancestors1 中的祖先
    std::queue<std::string> q2;
    std::set<std::string> visited2_bfs;

    q2.push(commit_hash2);
    visited2_bfs.insert(commit_hash2);

    while (!q2.empty()) {
        std::string current_c2 = q2.front();
        q2.pop();

        if (ancestors1.count(current_c2)) {
            return current_c2; // 找到了共同祖先
        }

        auto commit2_obj_opt = Commit::load_by_hash(current_c2, get_objects_directory());
        if (commit2_obj_opt) {
            for (const auto& parent_hash : commit2_obj_opt->parent_hashes_hex) {
                if (visited2_bfs.find(parent_hash) == visited2_bfs.end()) {
                    visited2_bfs.insert(parent_hash);
                    q2.push(parent_hash);
                }
            }
        }
    }

    return std::nullopt; // 没有找到共同祖先（理论上，在同一个仓库中最终会汇合到初始commit或空）
}


/**
 * @brief 在工作目录的文件中写入冲突标记，以展示来自不同来源（通常是 OURS 和 THEIRS）的内容。
 * @param relative_path 文件相对于仓库根目录的路径。
 * @param ours_label 标记“我们”这边版本的标签，例如 "HEAD" 或当前分支名。
 * @param ours_lines “我们”这边版本的文件内容（按行分割的列表）。如果为 std::nullopt 或空vector，表示此版本无内容或文件不存在。
 * @param theirs_label 标记“他们”那边版本的标签，例如被合并的分支名。
 * @param theirs_lines “他们”那边版本的文件内容（按行分割的列表）。如果为 std::nullopt 或空vector，表示此版本无内容或文件不存在。
 * @return 如果成功写入冲突标记文件，则返回 true；否则返回 false。
 */
bool Repository::_write_conflict_markers(
        const std::filesystem::path& relative_path,
        const std::string& ours_label,
        const std::optional<std::vector<std::string>>& ours_lines_opt,
        const std::string& theirs_label,
        const std::optional<std::vector<std::string>>& theirs_lines_opt) {

    std::filesystem::path abs_path = work_tree_root_ / relative_path;
    std::error_code ec_parent_dir;
    if (abs_path.has_parent_path()) {
        std::filesystem::create_directories(abs_path.parent_path(), ec_parent_dir);
        if (ec_parent_dir) {
            std::cerr << "错误: 无法创建父目录 " << abs_path.parent_path().string() << " 用于冲突文件: " << ec_parent_dir.message() << std::endl;
            return false;
        }
    }

    std::ofstream ofs(abs_path, std::ios::trunc); // 覆盖写入
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开文件 " << abs_path.string() << " 以写入冲突标记。" << std::endl;
        return false;
    }

    ofs << "<<<<<<< " << ours_label << "\n";
    if (ours_lines_opt && !ours_lines_opt->empty()) { // 确保有内容才遍历
        for (size_t i = 0; i < ours_lines_opt->size(); ++i) {
            ofs << (*ours_lines_opt)[i] << "\n";
        }
    } else if (ours_lines_opt && ours_lines_opt->empty()) {
        // 如果 ours_lines_opt 存在但是空的vector（例如，OURS版本是空文件），则不打印内容
    }
    // 如果 ours_lines_opt 是 std::nullopt (例如，OURS中文件被删除)，则不打印内容

    ofs << "=======\n";
    if (theirs_lines_opt && !theirs_lines_opt->empty()) { // 确保有内容才遍历
        for (size_t i = 0; i < theirs_lines_opt->size(); ++i) {
            ofs << (*theirs_lines_opt)[i] << "\n";
        }
    } else if (theirs_lines_opt && theirs_lines_opt->empty()) {
        // 同上
    }
    ofs << ">>>>>>> " << theirs_label << "\n";

    ofs.close();
    return ofs.good();
}


/** 加载所有配置到扁平化的 map
 * [remote "origin"]
 *  url = localhost:8080/userA/projectX   ->  (remote.origin.url , localhost:8080/userA/projectX )
 *  fetch = +refs/heads/*:refs/remotes/origin/*  ->  (remote.origin.fetch , +refs/heads/*:refs/remotes/origin/*)
 * [user]
 *  name = userA -> (user.name , userA)
 *  email = userA@email.com  -> (user.email , userA@email.com)
 **/
std::map<std::string, std::string> Repository::load_all_config() const {
    std::map<std::string, std::string> all_configs;
    std::filesystem::path config_path = get_config_file_path();
    if (!std::filesystem::exists(config_path)) {
        return all_configs;
    }

    std::ifstream ifs(config_path);
    std::string line;
    std::string current_section_name; // 例如 "core", "remote \"origin\"", "user"

    while (std::getline(ifs, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        // 注释
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line[0] == '[' && line.back() == ']') { // [section] 解析节名
            current_section_name = line.substr(1, line.length() - 2);
            // 对于 remote "origin"，current_section_name 就是 "remote \"origin\""
        } else if (!current_section_name.empty()) {  // 节下面是 键值对
            size_t equals_pos = line.find('=');
            if (equals_pos != std::string::npos) {
                std::string key = line.substr(0, equals_pos);  // key
                std::string value = line.substr(equals_pos + 1);   // value
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                // 构建扁平化的键
                std::string flat_key;
                // 处理 [remote "origin"] -> remote.origin.key
                if (current_section_name.rfind("remote \"", 0) == 0 && current_section_name.back() == '"') {
                    std::string remote_name = current_section_name.substr(std::string("remote \"").length());
                    remote_name.pop_back();
                    flat_key = "remote." + remote_name + "." + key;
                }
                // 处理普通节 [user] -> user
                else {
                    flat_key = current_section_name + "." + key;
                }
                all_configs[flat_key] = value;
            }
        }
    }
    return all_configs;
}

/** 保存扁平化的 map 回INI格式
 *INI格式
 * [remote "origin"]
 *  url = localhost:8080/userA/projectX
 *  fetch = +refs/heads/*:refs/remotes/origin/*
 * [user]
 *  name = userA
 *  email = userA@email.com
 ***/
bool Repository::save_all_config(const std::map<std::string, std::string>& all_configs) const {
    std::filesystem::path config_path = get_config_file_path();
    std::ofstream ofs(config_path, std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "错误: 无法打开配置文件进行写入: " << config_path.string() << std::endl;
        return false;
    }

    // 将扁平化的 map 重组成 按节(section) 和 子节(subsection, 仅用于remote) 的结构
    std::map<std::string, std::map<std::string, std::string>> sections; // section_name -> (key -> value)
    std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> sections_with_subsection; // section_name -> (subsection_name -> (key -> value))

    for (const auto& pair : all_configs) {
        const std::string& flat_key = pair.first;
        const std::string& value = pair.second;

        size_t first_dot = flat_key.find('.');
        if (first_dot == std::string::npos) continue; // 格式不对，跳过

        std::string section = flat_key.substr(0, first_dot);
        std::string rest = flat_key.substr(first_dot + 1);

        if (section == "remote") {
            size_t second_dot = rest.find('.');
            if (second_dot == std::string::npos) continue; // remote.name.key 格式
            std::string subsection_name = rest.substr(0, second_dot); // remote name
            std::string key_in_subsection = rest.substr(second_dot + 1);
            sections_with_subsection[section][subsection_name][key_in_subsection] = value;
        } else {
            sections[section][rest] = value; // 例如 user.name, core.filemode
        }
    }

    // 写入普通节
    for (const auto& section_pair : sections) {
        ofs << "[" << section_pair.first << "]" << std::endl;
        for (const auto& kv_pair : section_pair.second) {
            ofs << "\t" << kv_pair.first << " = " << kv_pair.second << std::endl;
        }
        ofs << std::endl;
    }

    // 写入带子节的节 (主要是 remote)
    for (const auto& section_pair : sections_with_subsection) { // section_pair.first == "remote"
        for (const auto& subsection_pair : section_pair.second) { // subsection_pair.first == "origin"
            ofs << "[" << section_pair.first << " \"" << subsection_pair.first << "\"]" << std::endl;
            for (const auto& kv_pair : subsection_pair.second) { // kv_pair.first == "url"
                ofs << "\t" << kv_pair.first << " = " << kv_pair.second << std::endl;
            }
            ofs << std::endl;
        }
    }

    ofs.close();
    return ofs.good();
}


bool Repository::is_fast_forward(const std::string& old_commit_hash, const std::string& new_commit_hash) const {
    if (old_commit_hash.empty()) { // 如果旧引用不存在（例如创建新分支），则任何提交都是“快进”
        return true;
    }
    if (old_commit_hash == new_commit_hash) { // 指向同一个commit，也是快进
        return true;
    }

    // 检查 new_commit_hash 是否能回溯到 old_commit_hash
    std::string current_hash = new_commit_hash;
    std::set<std::string> visited_commits; // 防止循环 (尽管在正常Git历史中不常见)
    int depth = 0;
    const int MAX_FF_DEPTH = 1000; // 限制查找深度，防止无限循环或性能问题

    while (!current_hash.empty() && depth < MAX_FF_DEPTH) {
        if (current_hash == old_commit_hash) {
            return true; // 找到了旧的 commit，是快进
        }
        if (visited_commits.count(current_hash)) {
            return false; // 遇到循环，不是快进
        }
        visited_commits.insert(current_hash);

        auto commit_opt = Commit::load_by_hash(current_hash, get_objects_directory()); //
        if (!commit_opt || commit_opt->parent_hashes_hex.empty()) {
            return false; // 无法加载commit，或到达历史起点仍未找到old_commit_hash
        }

        // 简化版：只要 old_commit 是 new_commit 的祖先之一就算。
        bool found_in_parents = false;
        for(const auto& parent_hash : commit_opt->parent_hashes_hex){
            if(parent_hash == old_commit_hash){
                return true;
            }
        }
        // 为了简化，我们只沿着第一个父节点回溯，这对于非合并历史是正确的
        current_hash = commit_opt->parent_hashes_hex[0];
        depth++;
    }
    return false; // 未找到，或超出深度
}


/**
 * @brief 找出在本地仓库中，从 tip_commit_hash（最新的提交）到 ancestor_commit_hash（一个较旧的共同祖先，但不包含它本身）之间的所有 commit 哈希
 * @param commits_to_send 结果存储在 commits_to_send 中，并且最终会反转顺序（最老的在前）
 * @return
 */
bool Repository::get_commits_between(const std::string& tip_commit_hash,
                                     const std::string& ancestor_commit_hash,
                                     std::vector<std::string>& commits_to_send) const {
    commits_to_send.clear();
    if (tip_commit_hash.empty() || (tip_commit_hash == ancestor_commit_hash && !ancestor_commit_hash.empty())) {
        return true; // 无需发送或已经是最新
    }

    std::string current_hash = tip_commit_hash; // 从最新的 commit 开始往回走
    std::set<std::string> visited;   // 用来记录已经访问过的 commit，防止在有问题的历史（例如循环）中无限递归
    const int MAX_PUSH_DEPTH = 500; // 推送深度限制
    int count = 0;

    // 循环条件：
    //    - current_hash 非空 (还没走到历史的尽头，即根 commit 还没有父 commit)。
    //    - current_hash 不等于 ancestor_commit_hash (还没遇到我们想停止的那个旧祖先)。
    //    - count < MAX_PUSH_DEPTH (还没超过最大回溯深度)。
    while (!current_hash.empty() && current_hash != ancestor_commit_hash && count < MAX_PUSH_DEPTH) {
        if (visited.count(current_hash)) {
            std::cerr << "Push Error (get_commits_between): Cycle detected at commit " << current_hash << std::endl;
            return false; // 历史中存在循环
        }
        visited.insert(current_hash);
        commits_to_send.push_back(current_hash); // 加入列表

        auto commit_opt = Commit::load_by_hash(current_hash, get_objects_directory());
        if (!commit_opt) {
            std::cerr << "Push Error (get_commits_between): Failed to load commit " << current_hash << " from local repository." << std::endl;
            return false; // 本地仓库缺少 commit 对象，数据不一致
        }

        if (commit_opt->parent_hashes_hex.empty()) { // 到达根提交
            if (!ancestor_commit_hash.empty()) {
                // 到达根了，但期望的祖先还没找到，说明 ancestor_commit_hash 不在当前历史链上
                std::cerr << "Push Error (get_commits_between): Reached root commit, but ancestor "
                          << ancestor_commit_hash << " was not found in history of " << tip_commit_hash << std::endl;
                return false;
            }
            break; // 注意这里 ancestor_commit_hash 为空 则会找到tip的所有commit
        }
        // 简单处理：只沿第一个父节点回溯。对于合并提交，这可能不完整。
        current_hash = commit_opt->parent_hashes_hex[0];
        count++;
    }

    if (count >= MAX_PUSH_DEPTH) {
        std::cerr << "Push Error (get_commits_between): Exceeded maximum push depth while searching for ancestor." << std::endl;
        return false;
    }

    // 如果 ancestor_commit_hash 非空，但循环结束时 current_hash 不是它，说明没找到共同祖先
    if (!ancestor_commit_hash.empty() && current_hash != ancestor_commit_hash) {
        std::cerr << "Push Error (get_commits_between): Could not find common ancestor " << ancestor_commit_hash
                  << " in history of " << tip_commit_hash << ". Remote history may have diverged." << std::endl;
        return false;
    }

    std::reverse(commits_to_send.begin(), commits_to_send.end());
    return true;
}


void Repository::collect_objects_recursive_for_push(const std::string& object_hash,
                                           std::set<std::string>& objects_to_collect,
                                           std::set<std::string>& visited_objects) const {
    if (object_hash.empty() || object_hash.length() != 40 || visited_objects.count(object_hash)) {
        return; // 哈希无效或已访问
    }
    visited_objects.insert(object_hash);

    if (!object_exists(object_hash)) { // 为了健壮性，可以再次检查
        std::cerr << "Warning (collect_objects_recursive): Object " << object_hash.substr(0,7) << " marked for collection but not found locally." << std::endl;
        return;
    }
    objects_to_collect.insert(object_hash); // 将当前对象加入待收集列表


    std::optional<std::filesystem::path> object_file_path_opt = _find_object_file_by_prefix(object_hash);
    if (!object_file_path_opt) {
        // object_exists 应该已经捕获了这个，但再次检查以防万一
        return;
    }


    // --- 先解析对象头部确定类型 ---
    std::optional<std::tuple<std::string, size_t, std::vector<std::byte>>> parsed_object_opt = read_and_parse_object_file_content(*object_file_path_opt);

    if (!parsed_object_opt) {
        std::cerr << "Warning (collect_objects_recursive): Failed to read or parse object header for " << object_hash.substr(0,7) << std::endl;
        return;
    }

    const auto& [type_str_read, content_size_read, raw_content_data] = *parsed_object_opt;

    // 根据解析出的类型，调用相应的加载和递归逻辑
    if (type_str_read == Commit::type_str()) {
        // 反序列化 commit 以获取 tree_hash (不需要完整的 Commit::load_by_hash 再序列化)
        auto commit_obj_opt = Commit::deserialize(raw_content_data); //
        if (commit_obj_opt) {
            if (!commit_obj_opt->tree_hash_hex.empty()) {
                collect_objects_recursive_for_push(commit_obj_opt->tree_hash_hex, objects_to_collect, visited_objects);
            }
        } else {
            std::cerr << "Warning (collect_objects_recursive): Failed to deserialize commit object " << object_hash.substr(0,7) << std::endl;
        }
    } else if (type_str_read == Tree::type_str()) { //
        // 反序列化 tree 以获取其条目
        auto tree_obj_opt = Tree::deserialize(raw_content_data); //
        if (tree_obj_opt) {
            for (const auto& entry : tree_obj_opt->entries) {
                collect_objects_recursive_for_push(entry.sha1_hash_hex, objects_to_collect, visited_objects);
            }
        } else {
            std::cerr << "Warning (collect_objects_recursive): Failed to deserialize tree object " << object_hash.substr(0,7) << std::endl;
        }
    } else if (type_str_read == Blob::type_str()) {
        // Blob 对象没有其他对象依赖，所以不需要进一步递归。它已经被加入 objects_to_collect 了。
    } else {
        std::cerr << "Warning (collect_objects_recursive): Unknown object type '" << type_str_read << "' for hash " << object_hash.substr(0,7) << std::endl;
    }
}

/**
 * @brief 检查具有给定名称的本地分支是否存在。
 * @param branch_name 要检查的分支的名称 (短名称，例如 "main")。
 * @return 如果分支存在，则返回 true；否则返回 false。
 */
bool Repository::branch_exists(const std::string& branch_name) const {
    if (branch_name.empty() || branch_name.find('/') != std::string::npos || branch_name.find('\\') != std::string::npos) {
        // 无效的分支名格式 (例如包含路径分隔符)
        return false;
    }

    std::filesystem::path branch_file_path = get_heads_directory() / branch_name; //
    std::error_code ec;

    // 检查文件是否存在并且是一个常规文件
    bool exists = std::filesystem::exists(branch_file_path, ec);
    if (ec) { // 检查 exists 操作本身是否出错
        std::cerr << "警告 (branch_exists): 检查分支文件 '" << branch_file_path.string() << "' 是否存在时出错: " << ec.message() << std::endl;
        return false; // 发生文件系统错误，保守地认为分支不存在或不可用
    }

    if (exists) {
        bool is_file = std::filesystem::is_regular_file(branch_file_path, ec);
        if (ec) { // 检查 is_regular_file 操作本身是否出错
            std::cerr << "警告 (branch_exists): 检查路径 '" << branch_file_path.string() << "' 是否为常规文件时出错: " << ec.message() << std::endl;
            return false;
        }
        return is_file; // 如果存在且是常规文件，则分支存在
    }

    return false; // 文件不存在
}


void Repository::collect_objects_for_commits(const std::vector<std::string>& commit_hashes,
                                             std::set<std::string>& objects_to_collect) const {
    objects_to_collect.clear();
    std::set<std::string> visited_during_collection;
    for (const std::string& commit_hash : commit_hashes) {
        collect_objects_recursive_for_push(commit_hash, objects_to_collect, visited_during_collection);
    }
}





// 路径与信息访问
const std::filesystem::path& Repository::get_work_tree_root() const {
    return work_tree_root_;
}
const std::filesystem::path& Repository::get_mygit_directory() const {
    return mygit_dir_;
}
std::filesystem::path Repository::get_objects_directory() const {
    return mygit_dir_ / OBJECTS_DIR_NAME;
}
std::filesystem::path Repository::get_refs_directory() const {
    return mygit_dir_ / REFS_DIR_NAME;
}
std::filesystem::path Repository::get_heads_directory() const {
    return mygit_dir_ / REFS_DIR_NAME / HEADS_DIR_NAME;
}

std::filesystem::path Repository::get_head_file_path() const {
    return mygit_dir_ / HEAD_FILE_NAME;
}
std::filesystem::path Repository::get_index_file_path() const {
    return mygit_dir_ / INDEX_FILE_NAME;
}

std::filesystem::path Repository::get_config_file_path() const {
    return mygit_dir_ / CONFIG_FILE_NAME;
}

std::filesystem::path Repository::get_tags_directory() const {
    return mygit_dir_ / REFS_DIR_NAME  / TAGS_DIR_NAME;
}

}
