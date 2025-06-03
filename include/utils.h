#pragma once
#include <string>
#include <fstream>
#include <vector>

#include <sstream>


namespace Utils {


/**
 * @brief 将字符串内容按行分割
 * @param content_str :
 * @return std::vector<std::string>按行分割
 */
std::vector<std::string> string_to_lines(const std::string& content_str);


enum class EditType {
    MATCH,  // 字符匹配
    INSERT, // 插入到序列 A
    DELETE  // 从序列 A 中删除字符
};

// 基于行的编辑操作
struct LineEditOperation {
    EditType type;
    std::string line_content; // 对于 INSERT/DELETE/MATCH，是涉及的【行】的内容
    int index_a;        // 操作在【旧行列表 A】中的原始行号索引 (0-based)。对于 INSERT，此值为 -1。
    int index_b;        // 操作在【新行列表 B】中的原始行号索引 (0-based)。对于 DELETE，此值为 -1。
};

/**
 * @brief Myers Diff 算法的line版
 * @param A: 源行列表 (std::vector<std::string>)
 * @param B: 目标行列表 (std::vector<std::string>)
 * @return std::vector<LineEditOperation> 从 A 转换为 B 的最短编辑脚本
 */
std::vector<LineEditOperation> MyersDiffLines(const std::vector<std::string>& A, const std::vector<std::string>& B);


/**
 * @brief 打印统一差异格式 (Unified Diff Format)
 * @param file_path:
 * @param ses:
 * @param num_context_lines :
 * @details
 *      文件识别头：diff --biogit a/your_file_path b/your_file_path
 *                --- a/your_file_path：代表旧版本文件
 *                +++ b/your_file_path：代表新版本文件
 *      差异块(Hunks)：
 *              Hunk 头部 (Hunk Header)：每个 Hunk 都以 @@ -old_start,old_length +new_start,new_length @@ 开头
 *                      -old_start,old_length：描述了这个 Hunk 在旧文件中的范围
 *                      +new_start,new_length：描述了这个 Hunk 在新文件中的范围
 *                      全新的（所有行都是添加），旧文件部分可能是 @@ -0,0 +1,N @@
 *                      被完全删除，新文件部分可能是 @@ -1,N +0,0 @@
 *              Hunk 内容行 (Diff Lines)：Hunk 头部之后是实际的差异行和上下文行，每行都以一个字符前缀开头
 *                      空格 ()：表示这是一个上下文行 (context line)，该行在旧版本和新版本中都存在且内容相同
 *                      减号 (-)：表示这是一个被删除的行 (deleted line)
 *                      加号 (+)：表示这是一个被添加的行 (added line)
 */
void print_unified_diff(
    const std::filesystem::path& file_path,
    const std::vector<LineEditOperation>& ses,
    const std::string& old_file_label_suffix,
    const std::string& new_file_label_suffix,
    int num_context_lines = 3);


/**
 * @brief 检查 target_path 是否等于 base_path_spec，或者是否是 base_dir_spec 的子目录/子文件。
 * @param target_path 要检查的目标路径。
 * @param base_dir_spec 作为基准的路径，用于判断 target_path 是否在其之下或与之相等。
 * @return 如果 target_path 等于或在 base_dir_spec 之下，则返回 true；否则返回 false。
 * @assumption 两个路径都应该是已经过规范化处理的、相对于同一个根目录的路径。
 */
bool is_path_under_or_equal(const std::filesystem::path& target_path, const std::filesystem::path& base_dir_spec);


// ---  Token ---
/**
 * @brief 获取当前仓库内 Token 存储文件的路径。
 * @param biogit_dir_path 指向当前仓库 .biogit 目录的路径。
 * @return Token 文件的完整路径 (例如 .biogit/token_credentials)。
 */
std::filesystem::path getRepositoryTokenFilePath(const std::filesystem::path& biogit_dir_path);

/**
 * @brief 将认证Token保存到指定仓库的配置文件中。
 * 会覆盖已有的Token。
 * @param biogit_dir_path 指向当前仓库 .biogit 目录的路径。
 * @param token 要保存的Token字符串。
 * @return 如果保存成功，返回 true；否则返回 false。
 */
bool saveRepositoryToken(const std::filesystem::path& biogit_dir_path, const std::string& token);

/**
 * @brief 从指定仓库的配置文件中加载认证Token。
 * @param biogit_dir_path 指向当前仓库 .biogit 目录的路径。
 * @return 如果找到并成功加载Token，返回包含Token的 std::optional<std::string>。
 * 如果文件不存在或读取失败，返回 std::nullopt。
 */
std::optional<std::string> loadRepositoryToken(const std::filesystem::path& biogit_dir_path);

/**
 * @brief 从指定仓库的配置文件中清除（删除）认证Token。
 * @param biogit_dir_path 指向当前仓库 .biogit 目录的路径。
 * @return 如果Token文件被成功删除或原本就不存在，返回 true；否则返回 false。
 */
bool clearRepositoryToken(const std::filesystem::path& biogit_dir_path);

}
