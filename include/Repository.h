#pragma once

// 标准库依赖
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <set>
#include <optional>

// 项目内部依赖
#include "sha1.h"       // SHA1 哈希计算
#include "Index.h"      // 索引/暂存区管理

namespace Biogit {

using std::string;
using SHA1::sha1;


/**
 * @brief Diff 操作的选项配置。默认比较工作目录和暂存区
 */
struct DiffOptions {
    bool staged = false;              ///< true: 比较暂存区和最近提交 (HEAD)；false (默认): 比较工作目录和暂存区
    std::string commit1_hash_str;     ///< 第一个参与比较的 Commit 标识符 (哈希、分支名、标签名等)
    std::string commit2_hash_str;     ///< 第二个参与比较的 Commit 标识符
    std::vector<std::filesystem::path> paths_to_diff; ///< 要进行 diff 的特定文件或目录路径列表。如果为空，则比较所有涉及的路径

    DiffOptions() : staged(false) {}  // 默认构造函数
};

/**
 * @brief 远程仓库配置信息。
 */
struct RemoteConfig {
    std::string url;            ///< 远程仓库的 URL
    std::string fetch_refspec;  ///< 获取操作的引用规范 (refspec)

};

/**
 * @brief 用户配置信息 (用于 Commit 的作者/提交者)。
 */
struct UserConfig {
    std::string name;       ///< 用户名
    std::string email;      ///< 用户邮箱
};


// --- Repository 类声明 ---

/**
 * @brief 代表一个 BioGit 仓库，封装了所有仓库级别的操作和数据管理。
 * @details
 * 管理 .biogit 目录结构，包括对象存储、引用、索引、配置等。
 * 提供了初始化、加载、添加文件、提交、分支管理、标签管理、合并、
 * 差异比较、远程操作等核心 Git 功能。
 *
 * .biogit 目录结构示意:
 * .biogit/
 * |-- objects/         # 存储 Git 对象 (commit, tree, blob)
 * |-- refs/            # 存储引用
 * |   |-- heads/       # 存储本地分支指针
 * |   |-- tags/        # 存储标签指针
 * |   `-- remotes/     # (隐式) 存储远程跟踪分支，通过 fetch 创建
 * |-- HEAD             # 指向当前活动的分支或 Commit (分离头指针)
 * |-- index            # 暂存区文件
 * |-- config           # 本地仓库配置文件
 * |-- MERGE_HEAD       # (可选) 合并操作中，记录正在被合并的 Commit 哈希
 * |-- BIOGIT_CONFLICTS # (可选) 合并冲突时，记录冲突文件列表
 * `-- biogit_token     # (可选) 保存当前仓库与远程服务器交互的认证 Token (由 login 命令写入)
 */
class Repository {
public:
    // --- 静态常量：定义 .biogit 仓库内部的关键目录和文件名 ---
    static const std::string MYGIT_DIR_NAME;     ///< .biogit 目录的名称。
    static const std::string OBJECTS_DIR_NAME;   ///< objects 子目录的名称。
    static const std::string REFS_DIR_NAME;      ///< refs 子目录的名称。
    static const std::string HEADS_DIR_NAME;     ///< refs/heads 子目录的名称。
    static const std::string TAGS_DIR_NAME;      ///< refs/tags 子目录的名称。

    static const std::string HEAD_FILE_NAME;       ///< HEAD 文件的名称。
    static const std::string MERGE_HEAD_FILE_NAME; ///< MERGE_HEAD 文件的名称 (合并时使用)。
    static const std::string FILE_CONFLICTS;       ///< BIOGIT_CONFLICTS 文件的名称 (记录合并冲突)。
    static const std::string INDEX_FILE_NAME;      ///< index (暂存区) 文件的名称。
    static const std::string CONFIG_FILE_NAME;     ///< 本地仓库配置文件名。


    // --- 构造与加载 ---
    /**
     * @brief 初始化一个新的 BioGit 仓库。
     * @param work_tree_path 仓库的工作树根目录路径。默认为当前工作目录。
     * @return 如果成功，返回一个包含 Repository 对象的 std::optional；否则返回 std::nullopt。
     */
    static std::optional<Repository> init(const std::filesystem::path& work_tree_path = std::filesystem::current_path());

    /**
     * @brief 加载一个已存在的 BioGit 仓库。
     * @param work_tree_path 仓库的工作树根目录路径。
     * @return 如果成功，返回一个包含 Repository 对象的 std::optional；否则返回 std::nullopt。
     */
    static std::optional<Repository> load(const std::filesystem::path& work_tree_path);

    /**
     * @brief 从远程 URL 克隆一个仓库到本地。
     * @param remote_url_str 远程仓库的 URL。
     * @param target_directory_path 本地克隆的目标目录路径。
     * @return 如果成功，返回一个包含新创建的 Repository 对象的 std::optional；否则返回 std::nullopt。
     */
    static std::optional<Repository> clone(const std::string& remote_url_str,const std::filesystem::path& target_directory_path);

    // --- 核心本地操作 ---
    /**
     * @brief 将指定路径的文件或目录内容添加到索引 (暂存区)。
     * @param path_to_add 要添加的路径。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool add(const std::filesystem::path& path_to_add);

    /**
     * @brief 从索引中移除指定路径的文件，但保留工作目录中的文件 (类似 git rm --cached)。
     * @param path_to_remove 要从索引中移除的路径。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool rm_cached(const std::filesystem::path& path_to_remove);

    /**
         * @brief 从工作目录和索引中移除指定路径的文件 (类似 git rm)。
         * @param path_to_remove 要移除的路径。
         * @return 如果成功，返回 true；否则返回 false。
         */
    bool rm(const std::filesystem::path& path_to_remove);

    /**
     * @brief 将暂存区的更改记录为一个新的 Commit。
     * @param message 提交信息。
     * @return 如果成功，返回新 Commit 的 SHA-1 哈希的 std::optional；否则返回 std::nullopt。
     */
    std::optional<std::string> commit(const std::string& message);

    // --- 信息展示与比较 ---
    /**
     * @brief 显示当前仓库的工作区、暂存区和HEAD之间的状态。
     */
    void status() const;

    /**
     * @brief 显示当前分支的提交历史 (从 HEAD 开始回溯)。
     */
    void log() const;

    /**
     * @brief 根据提供的选项显示差异。
     * @param options Diff 操作的配置选项。
     */
    void diff(const DiffOptions& options);

    // --- 分支管理 ---
    /**
     * @brief 创建一个新的本地分支。
     * @param branch_name 新分支的名称。
     * @param start_point_commit_hash_param (可选) 新分支基于的Commit的SHA-1哈希。如果为空，则基于当前HEAD指向的Commit创建。
     * @return 如果分支创建成功，返回 true；否则返回 false (例如分支已存在，或起点Commit无效)。
     */
    bool branch_create(const std::string& branch_name, const std::string& start_point_commit_hash_param="");

    /**
     * @brief 列出所有本地分支，并高亮当前活动分支。
     */
    void branch_list() const;

    /**
     * @brief 删除一个本地分支。
     * @param branch_name 要删除的分支名称。
     * @param force (可选) 是否强制删除。默认为 false。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool branch_delete(const std::string& branch_name, bool force = false);

    /**
     * @brief 切换到指定的分支或 Commit (分离 HEAD)。
     * @param target_identifier 目标分支名称、Commit 哈希或标签名。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool switch_branch(const std::string& target_identifier);

    // --- 标签管理 ---
    /**
     * @brief 创建一个新的轻量标签。
     * @param tag_name 新标签的名称。
     * @param commit_ish_str (可选) 标签指向的 Commit 标识符。如果为空，则指向当前 HEAD。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool tag_create(const std::string& tag_name, const std::string& commit_ish_str = "");

    /**
     * @brief 列出所有本地标签。
     */
    void tag_list() const;

    /**
     * @brief 删除一个本地标签。
     * @param tag_name 要删除的标签名称。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool tag_delete(const std::string& tag_name);

    // --- 合并操作 ---
    /**
     * @brief 将指定分支的更改合并到当前活动分支。
     * @param branch_to_merge_name 要合并的分支的名称或 Commit 标识符。
     * @return 如果合并成功 (包括快进或无冲突三路合并并自动提交)，返回 true；
     * 如果发生需要手动解决的冲突或错误，返回 false。
     */
    bool merge(const std::string& branch_to_merge_name);


    // --- 远程仓库配置 ---
    /**
     * @brief 添加一个新的远程仓库配置。
     * @param name 远程仓库的别名 (例如 "origin")。
     * @param url 远程仓库的 URL。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool remote_add(const std::string& name, const std::string& url);

    /**
     * @brief 移除一个已配置的远程仓库。
     * @param name 要移除的远程仓库的别名。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool remote_remove(const std::string& name);

    /**
     * @brief 获取指定远程仓库的配置信息。
     * @param name 远程仓库的别名。
     * @return 如果找到，返回包含 RemoteConfig 的 std::optional；否则返回 std::nullopt。
     */
    std::optional<RemoteConfig> remote_get_config(const std::string& name) const;

    /**
     * @brief 列出所有已配置的远程仓库及其主要配置。
     * @return 一个从远程名称到 RemoteConfig 的映射。
     */
    std::map<std::string, RemoteConfig> remote_list_configs() const;


    // --- 本地仓库配置 (user.name, user.email 等) ---
    /**
     * @brief 设置一个配置项的值。
     * @param key 配置项的键 (例如 "user.name")。
     * @param value 配置项的值。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool config_set(const std::string& key, const std::string& value);

    /**
     * @brief 获取一个配置项的值。
     * @param key 配置项的键。
     * @return 如果找到，返回包含配置值的 std::optional；否则返回 std::nullopt。
     */
    std::optional<std::string> config_get(const std::string& key) const;

    /**
     * @brief 获取所有本地仓库配置项。
     * @return 一个从键到值的映射。
     */
    std::map<std::string, std::string> config_get_all() const;

    /**
     * @brief 获取用户配置信息 (name 和 email)。
     * @return 如果配置存在，返回包含 UserConfig 的 std::optional；否则返回 std::nullopt。
     */
    std::optional<UserConfig> get_user_config() const;


    // --- 服务器端交互 API (供 LogicSystem 调用) ---
    /**
     * @brief 获取仓库中所有本地引用（分支和标签）及其指向的 Commit 哈希。
     * @return 一个包含 <引用全名, Commit哈希> 对的向量。
     */
    std::vector<std::pair<std::string, std::string>> get_all_local_refs() const;

    /**
     * @brief 根据对象哈希读取对象的完整原始文件内容 (包括头部)。
     * @param object_hash 对象的 SHA-1 哈希或唯一前缀。
     * @return 如果找到，返回包含对象原始内容的 std::vector<char>；否则返回 std::nullopt。
     */
    std::optional<std::vector<char>> get_raw_object_content(const std::string& object_hash) const;

    /**
     * @brief 检查具有给定哈希的对象是否存在于仓库中。
     * @param object_hash 对象的 SHA-1 哈希或唯一前缀。
     * @return 如果对象存在，返回 true；否则返回 false。
     */
    bool object_exists(const std::string& object_hash) const;

    /**
     * @brief 将对象的原始序列化数据直接写入对象库。
     * @param object_hash 对象的 SHA-1 哈希 (应由调用者预先计算或验证)。
     * @param raw_data 指向对象完整原始内容数据 (包括头部) 的指针。
     * @param length raw_data 的长度。
     * @return 如果写入成功或对象已存在，返回 true；否则返回 false。
     */
    bool write_raw_object(const std::string& object_hash, const char* raw_data, uint32_t length);

    /**
     * @brief 更新或创建引用的结果枚举。
     */
    enum class UpdateRefResult {
        SUCCESS,                  ///< 引用成功更新或创建。
        REF_NOT_FOUND_FOR_UPDATE, ///< 若提供了 expected_old_hash 但引用不存在 (用于更新场景)。
        OLD_HASH_MISMATCH,        ///< 若提供了 expected_old_hash 但不匹配当前值。
        NEW_COMMIT_NOT_FOUND,     ///< new_commit_hash 指向的对象不存在或不是 Commit。
        NOT_FAST_FORWARD,         ///< 更新不是快进 (主要针对分支)。
        IO_ERROR,                 ///< 文件读写错误。
        INVALID_REF_NAME,         ///< 引用名称格式无效。
        UNKNOWN_ERROR             ///< 其他未知错误。
    };

    /**
     * @brief (服务器端) 更新或创建指定的引用，使其指向新的 Commit 哈希。
     * @param ref_full_name 要更新的引用的完整名称 (例如 "refs/heads/main")。
     * @param new_commit_hash 新的 Commit SHA-1 哈希。
     * @param expected_old_commit_hash (可选) 客户端期望该引用当前指向的旧 Commit 哈希。
     * @param allow_non_fast_forward (可选) 是否允许非快进更新。默认为 false。
     * @return 更新操作的结果 (UpdateRefResult 枚举)。
     */
    UpdateRefResult update_ref(const std::string& ref_full_name,
                               const std::string& new_commit_hash,
                               const std::optional<std::string>& expected_old_commit_hash = std::nullopt,
                               bool allow_non_fast_forward = false);


    // --- 客户端远程操作 ---
    /**
     * @brief (客户端) 将本地分支的更改推送到指定的远程仓库。
     * @param remote_name 要推送到的远程仓库的别名。
     * @param local_ref_full_name 要推送的本地引用的完整名称 (例如 "refs/heads/main") 或短名称 ("main")。
     * @param remote_ref_full_name_on_server 要在远程仓库上更新或创建的引用的完整名称。
     * @param force 是否执行强制推送。
     * @param token 用于向服务器进行身份验证的认证 Token。
     * @return 如果推送成功，返回 true；否则返回 false。
     */
    bool push(const std::string& remote_name,
              const std::string& local_ref_full_name, // e.g., "main" or "refs/heads/main"
              const std::string& remote_ref_full_name_on_server, // e.g., "main" or "refs/heads/main"
              bool force,
              const std::string& token);

    /**
     * @brief (客户端) 从指定的远程仓库获取更新。
     * @param remote_name 要从中获取的远程仓库的别名。
     * @param token 用于向服务器进行身份验证的认证 Token。
     * @param ref_to_fetch_param (可选) 要获取的特定引用名称。如果为空，则获取所有符合 refspec 的引用。
     * @return 如果获取成功，返回 true；否则返回 false。
     */
    bool fetch(const std::string& remote_name,
               const std::string& token,
               const std::string& ref_to_fetch_param = ""); // 默认为空，表示 fetch all (根据refspec)


    /**
     * @brief (客户端) 从指定的远程仓库获取更新，并将其合并到当前检出的本地分支。
     * @param remote_name 要从中拉取的远程仓库的别名。
     * @param remote_branch_name 要从远程仓库拉取的特定分支的名称。
     * @param token 用于向服务器进行身份验证的认证 Token。
     * @return 如果整个 pull 操作成功，返回 true；否则返回 false。
     */
    bool pull(const std::string& remote_name,
              const std::string& remote_branch_name,
              const std::string& token);

    // --- 路径与信息访问 (供内部和可能的外部工具使用) ---
    /** @brief 获取工作树的根目录路径。*/
    const std::filesystem::path& get_work_tree_root() const;
    /** @brief 获取 .biogit 目录的路径。*/
    const std::filesystem::path& get_mygit_directory() const;
    /** @brief 获取 objects 目录的路径。*/
    std::filesystem::path get_objects_directory() const;
    /** @brief 获取 refs 目录的路径。*/
    std::filesystem::path get_refs_directory() const;
    /** @brief 获取 refs/heads 目录的路径。*/
    std::filesystem::path get_heads_directory() const;
    /** @brief 获取 refs/tags 目录的路径。*/
    std::filesystem::path get_tags_directory() const;
    /** @brief 获取 HEAD 文件的路径。*/
    std::filesystem::path get_head_file_path() const;
    /** @brief 获取 index 文件的路径。*/
    std::filesystem::path get_index_file_path() const;
    /** @brief 获取本地仓库 config 文件的路径。*/
    std::filesystem::path get_config_file_path() const;


    /**
     * @brief (内部) 从给定的起始路径向上查找 .biogit 仓库的工作树根目录。
     */
    static std::optional<std::filesystem::path> find_repository_root(const std::filesystem::path& starting_path);

    /**
     * @brief (公开API) 根据给定的 SHA-1 哈希 (或唯一前缀) 加载并显示对象内容。
     * @param object_hash_prefix 对象的哈希前缀。
     * @param pretty_print (可选) 是否以更易读的格式打印 (类似 git cat-file -p)。默认为 true。
     * @return 如果成功，返回 true；否则返回 false。
     */
    bool show_object_by_hash(const std::string &object_hash_prefix, bool pretty_print=true);

private:
    /**
     * @brief (私有构造函数) 通过工作树路径创建 Repository 实例。
     * @details 强制通过静态工厂方法 init() 或 load() 创建实例。
     * @param work_tree_path 仓库的工作树根目录绝对路径。
     */
    explicit Repository(const std::filesystem::path& work_tree_path);


    /**
     * @brief (内部) 将用户提供的路径转换为规范化的、相对于仓库工作树根目录的相对路径。
     */
    std::optional<std::filesystem::path> normalize_and_relativize_path(const std::filesystem::path& user_path) const;

    /**
     * @brief (内部) 从已排序的索引条目构建层级的 Tree 对象，并返回根 Tree 对象的 SHA-1 哈希。
     */
    std::optional<std::string> _build_trees_and_get_root_hash(const std::vector<IndexEntry>& sorted_index_entries);

    /**
     * @brief (内部) 获取当前 HEAD 指向的 Commit 的 SHA-1 哈希。
     */
    std::optional<std::string> _get_head_commit_hash() const;

    /**
     * @brief (内部) 解析 Commit-ish 字符串 (分支名, "HEAD", 标签名, 哈希前缀, 完整哈希) 为完整的 Commit SHA-1 哈希。
     */
    std::optional<std::string> _resolve_commit_ish_to_full_hash(const std::string &name_or_hash_prefix) const;

    /**
     * @brief (内部) 递归加载指定 Tree 对象及其子 Tree 中的所有文件条目到映射中。
     * @param tree_hash_hex 要加载的 Tree 对象的哈希。
     * @param current_path_prefix 当前路径前缀，用于构建文件的完整相对路径。
     * @param files_map 输出参数，存储 <文件相对路径, <Blob哈希, 文件模式>> 的映射。
     */
    void _load_tree_contents_recursive(
        const std::string& tree_hash_hex,
        const std::filesystem::path& current_path_prefix,
        std::map<std::filesystem::path, std::pair<std::string, std::string>>& files_map
    ) const;

    /**
     * @brief (内部) 从给定的 Tree 哈希递归地将文件条目填充到 Index 对象中。
     * @param tree_hash_hex 要加载的 Tree 对象的哈希。
     * @param current_path_prefix 当前路径前缀。
     * @param target_index 要填充的 Index 对象的引用。
     */
    void _populate_index_from_tree_recursive(
        const std::string& tree_hash_hex,
        const std::filesystem::path& current_path_prefix,
        Index& target_index // 直接修改传入的 Index 对象
    ) const;

    /**
     * @brief (内部) 根据目标 Commit 的根 Tree 对象的内容，更新工作目录中的文件。
     * @param target_root_tree_hash 目标根 Tree 对象的哈希。
     * @param current_head_files_map 当前 HEAD Commit 的文件列表 (用于比较和删除旧文件)。
     * @return 如果成功更新工作目录，返回 true；否则返回 false。
     */
    bool _update_working_directory_from_tree(
        const std::string& target_root_tree_hash,
        const std::map<std::filesystem::path, std::pair<std::string, std::string>>& current_head_files_map);

    /**
     * @brief (内部) 读取并解析 Git 对象文件，分离出类型、大小和原始内容数据。
     * @param file_path 对象文件的路径。
     * @return 如果成功，返回一个包含 <类型字符串, 内容大小, 内容字节向量> 的元组；否则返回 std::nullopt。
     */
    std::optional<std::tuple<std::string, size_t, std::vector<std::byte>>> read_and_parse_object_file_content(const std::filesystem::path& file_path) const;

    /**
     * @brief (内部) 通过 SHA-1 哈希前缀查找唯一的对象文件路径。
     * @param hash_prefix 哈希前缀 (至少6个字符)。
     * @return 如果找到唯一匹配的对象文件，返回其路径；否则返回 std::nullopt。
     */
    std::optional<std::filesystem::path> _find_object_file_by_prefix(const std::string& hash_prefix) const;

    /**
     * @brief (内部) 获取指定 Blob 哈希对应的内容，并按行分割。
     */
    std::optional<std::vector<std::string>> _get_blob_lines(const std::string& blob_hash) const;

    /**
     * @brief (内部) 检查当前工作区和索引相对于 HEAD 是否“干净”(即没有未提交的更改)。
     */
    bool is_workspace_clean() const;

    /**
     * @brief (内部) 获取工作目录中指定相对路径文件的内容，并按行分割。
     */
    std::optional<std::vector<std::string>> _get_workdir_lines(const std::filesystem::path& relative_path) const;

    /**
     * @brief (内部) 执行文件内容的差异比较，并以统一差异格式打印结果。
     */
    void _perform_and_print_file_diff(
        const std::filesystem::path &display_path,
        const std::vector<std::string> &lines_a,
        const std::string &label_a_suffix,
        const std::vector<std::string> &lines_b,
        const std::string &label_b_suffix);

    /**
     * @brief (内部) 查找两个 Commit 之间的最近共同祖先 (LCA)。
     */
    std::optional<std::string> _find_common_ancestor(const std::string& commit_hash1, const std::string& commit_hash2) const;

    /**
     * @brief (内部) 在合并冲突时，向工作目录的文件中写入冲突标记。
     */
    bool _write_conflict_markers(
        const std::filesystem::path& relative_path,
        const std::string& ours_label,
        const std::optional<std::vector<std::string>>& ours_lines,
        const std::string& theirs_label,
        const std::optional<std::vector<std::string>>& theirs_lines
    );

    /**
        * @brief (内部) 从本地 .biogit/config 文件加载所有配置项到扁平化的映射中。
        */
    std::map<std::string, std::string> load_all_config() const;

    /**
     * @brief (内部) 将扁平化的配置映射保存回本地 .biogit/config 文件 (INI 格式)。
     */
    bool save_all_config(const std::map<std::string, std::string>& all_configs) const; // 修改为 const

    /**
     * @brief (内部) 检查从 old_commit_hash 到 new_commit_hash 是否是快进关系。
     */
    bool is_fast_forward(const std::string& old_commit_hash, const std::string& new_commit_hash) const;

    /**
     * @brief (内部) 获取从 tip_commit_hash 回溯到 ancestor_commit_hash (不含) 之间的所有 Commit 哈希列表。
     * @details 结果列表按从旧到新的顺序排列。
     */
    bool get_commits_between(const std::string& tip_commit_hash,
                             const std::string& ancestor_commit_hash,
                             std::vector<std::string>& commits_to_send) const;

    /**
     * @brief (内部) 为给定的 Commit 哈希列表收集所有相关的 Git 对象哈希 (Commit, Tree, Blob)。
     */
    void collect_objects_for_commits(const std::vector<std::string>& commit_hashes,
                                     std::set<std::string>& objects_to_collect) const;
    /**
     * @brief (内部，用于 push) 递归地收集从指定对象哈希可达的所有 Git 对象。
     */
    void collect_objects_recursive_for_push(const std::string& object_hash,
                                            std::set<std::string>& objects_to_collect,
                                            std::set<std::string>& visited_objects) const;

    /**
     * @brief (内部) 检查具有给定名称的本地分支是否存在。
     */
    bool branch_exists(const std::string& branch_name) const;


private:
    std::filesystem::path work_tree_root_; ///< 工作树的根目录绝对路径。
    std::filesystem::path mygit_dir_;      ///< .biogit 目录的绝对路径。
    Index index_manager_;                  ///< 索引 (暂存区) 管理器实例。
};




}


