#include "../include/utils.h"

#include <iostream>
#include <numeric>

namespace Utils{

std::vector<std::string> string_to_lines(const std::string& content_str) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream iss(content_str);
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}


std::vector<LineEditOperation> MyersDiffLines(const std::vector<std::string>& A_lines, const std::vector<std::string>& B_lines) {
    const int N = A_lines.size();
    const int M = B_lines.size();

    // 最大编辑距离
    const int MAX_D = N + M;
    const int offset = MAX_D;

    // V 数组定义和初始化
    std::vector<int> V(2 * MAX_D + 1, -1);
    std::vector<std::vector<int>> v_history;
    v_history.reserve(MAX_D + 2);

    V[offset + 1] = 0;
    int final_d = -1;

     // --- 阶段 1: 计算最短编辑距离 D 和 V 数组历史 ---
    for (int d = 0; d <= MAX_D; ++d) { 
        v_history.push_back(V); 
        // k的取值[-d,d] 且步长 2
        for (int k = -d; k <= d; k += 2) { 
            int k_idx = k + offset;      
            int x_start;                 

            // 判断 删除A 还是 插入B
            if (k == -d || (k != d && V[k_idx - 1] < V[k_idx + 1])) { 
                x_start = V[k_idx + 1]; 
            } else {
                x_start = V[k_idx - 1] + 1; 
            }

            int y_start = x_start - k; 

            int x = x_start; 
            int y = y_start; 

            // 尽可能对角线匹配
            while (x < N && y < M && A_lines[x] == B_lines[y]) {
                x++;
                y++;
            }

            V[k_idx] = x; 

            // 到达终点
            if (x >= N && y >= M) { 
                final_d = d;        
                goto found_solution; 
            }
        }
    }

found_solution:
    if (final_d == -1) { 
         if (N ==0 && M == 0) final_d = 0;
         else return {};
    }

    v_history.push_back(V); 

    // --- 阶段 2: 路径回溯，构建最短编辑脚本 (SES) ---
    std::vector<LineEditOperation> ses;
    int current_x = N;
    int current_y = M;

    // v[d+1]  v[d] 判断第d步选择的策略
    for (int d = final_d; d > 0; --d) { 
        const std::vector<int>& V_prev_state = v_history[d]; 
        int k = current_x - current_y; 

        int prev_x_from_k_minus_1 = abs(k - 1) <= (d - 1)
                                     ? V_prev_state[(k - 1) + offset]
                                     : std::numeric_limits<int>::min();

        int prev_x_from_k_plus_1  = abs(k + 1) <= (d - 1)
                                     ? V_prev_state[(k + 1) + offset]
                                     : std::numeric_limits<int>::min();
        bool chose_insertion_in_forward;
        if (k == -d || (k != d && prev_x_from_k_minus_1 < prev_x_from_k_plus_1)) {
            chose_insertion_in_forward = true;
        } else {
            chose_insertion_in_forward = false;
        }


        int x_after_edit_before_snake, y_after_edit_before_snake; 
        if (chose_insertion_in_forward) {
            x_after_edit_before_snake = prev_x_from_k_plus_1;
        } else {
            x_after_edit_before_snake = prev_x_from_k_minus_1 + 1;
        }
        y_after_edit_before_snake = x_after_edit_before_snake - k;


        int temp_x = current_x; 
        int temp_y = current_y; 
        while (temp_x > x_after_edit_before_snake || temp_y > y_after_edit_before_snake) {
            ses.push_back({EditType::MATCH, A_lines[temp_x - 1], temp_x - 1, temp_y - 1});
            temp_x--;
            temp_y--;
        }

        if (chose_insertion_in_forward) {
            // B_lines[y_after_edit_before_snake - 1] 是被插入的行
            std::string inserted_line_content = (y_after_edit_before_snake > 0) ? B_lines[y_after_edit_before_snake - 1] : ""; // 安全获取行
            int inserted_idx = y_after_edit_before_snake - 1;
            ses.push_back({EditType::INSERT, inserted_line_content, -1, inserted_idx});

            current_x = x_after_edit_before_snake;         
            current_y = y_after_edit_before_snake - 1;     
        } else { // chose_deletion
            // A_lines[x_after_edit_before_snake - 1] 是被删除的行
            std::string deleted_line_content = (x_after_edit_before_snake > 0) ? A_lines[x_after_edit_before_snake - 1] : ""; // 安全获取行
            int deleted_idx = x_after_edit_before_snake - 1;
            ses.push_back({EditType::DELETE, deleted_line_content, deleted_idx, -1});

            current_x = x_after_edit_before_snake - 1;     
            current_y = y_after_edit_before_snake;         
        }
    }

    while (current_x > 0 || current_y > 0) { 
        ses.push_back({EditType::MATCH, A_lines[current_x - 1], current_x - 1, current_y - 1});
        current_x--;
        current_y--;
    }

    std::reverse(ses.begin(), ses.end()); 
    return ses;
}


void print_unified_diff(
    const std::filesystem::path& file_path,
    const std::vector<LineEditOperation>& ses, // 编辑脚本
    const std::string& old_file_label_suffix,
    const std::string& new_file_label_suffix,
    int num_context_lines ) { // 上下文行数

    // 1. 初步检查：如果编辑脚本为空，或所有操作都是 MATCH，则没有差异可显示。
    bool has_actual_changes = false;
    if (ses.empty()) {
        // 注意：Repository::diff() 在调用此函数前，通常已通过哈希比较排除了文件内容完全相同的情况。
        // 如果 ses 为空，可能表示输入文件本身为空，或者 Myers 实现对空文件返回空 ses。
        return;
    }
    for (const auto& op : ses) {
        if (op.type != EditType::MATCH) {
            has_actual_changes = true;
            break;
        }
    }
    if (!has_actual_changes) {
        return; // 文件内容相同
    }

    // 2. 打印文件识别头
    std::cout << "diff --biogit a/" << file_path.generic_string() << " b/" << file_path.generic_string() << std::endl;
    std::cout << "--- a/" << file_path.generic_string() << old_file_label_suffix << std::endl;
    std::cout << "+++ b/" << file_path.generic_string() << new_file_label_suffix << std::endl;

    int current_op_idx = 0; // 当前处理到的 SES 操作的索引
    while (current_op_idx < ses.size()) {
        // 3. 寻找下一个 Hunk 的起点
        //    一个 Hunk 开始于一个变化（INSERT/DELETE）之前 num_context_lines 行的 MATCH（前导上下文），
        //    或者如果变化就在文件开头附近，则从文件开头开始。

        int first_change_op_idx = current_op_idx; // Hunk 内第一个非 MATCH 操作的索引
        // 跳过当前位置开始的 MATCH 行，找到第一个实际的更改
        while (first_change_op_idx < ses.size() && ses[first_change_op_idx].type == EditType::MATCH) {
            first_change_op_idx++;
        }

        if (first_change_op_idx == ses.size()) {
            break; // 从 current_op_idx 开始全是 MATCH，diff 结束
        }

        // Hunk 在 SES 中的起始操作索引（包含前导上下文）
        int hunk_start_in_ses = std::max(0, first_change_op_idx - num_context_lines);

        // 4. 寻找这个 Hunk 的结束点
        //    Hunk 结束于最后一个变化之后 num_context_lines 行的 MATCH（尾随上下文），
        //    或者如果变化延伸到文件末尾，则在文件末尾结束。

        int last_change_op_idx = first_change_op_idx; // Hunk 内最后一个非 MATCH 操作的索引
        for (int i = first_change_op_idx; i < ses.size(); ++i) {
            if (ses[i].type != EditType::MATCH) {
                last_change_op_idx = i;
            } else {
                // 如果当前 MATCH 行与 Hunk 内最后一个变化 (last_change_op_idx)
                // 的距离已达到或超过 num_context_lines，说明尾随上下文足够了，
                // 这个 MATCH 不属于当前 Hunk 的核心变化+尾随上下文部分，Hunk 在此之前结束。
                if ((i - last_change_op_idx -1) >= num_context_lines) {
                    break;
                }
            }
        }
        // Hunk 实际打印到的 SES 索引（开区间，即不包含此索引指向的操作）
        int hunk_end_in_ses = std::min((int)ses.size(), last_change_op_idx + num_context_lines + 1);


        // 5. 计算 Hunk 头部信息：@@ -old_s,old_l +new_s,new_l @@
        int old_s = 0; // 旧文件的起始行号 (1-based)
        int old_l = 0; // 旧文件在此 Hunk 中的行数
        int new_s = 0; // 新文件的起始行号 (1-based)
        int new_l = 0; // 新文件在此 Hunk 中的行数

        bool old_s_found = false;
        bool new_s_found = false;

        for (int i = hunk_start_in_ses; i < hunk_end_in_ses; ++i) {
            const auto& op = ses[i];
            if (op.type == EditType::MATCH || op.type == EditType::DELETE) {
                old_l++;
                if (!old_s_found && op.index_a != -1) { // op.index_a 是 0-based
                    old_s = op.index_a + 1;
                    old_s_found = true;
                }
            }
            if (op.type == EditType::MATCH || op.type == EditType::INSERT) {
                new_l++;
                if (!new_s_found && op.index_b != -1) { // op.index_b 是 0-based
                    new_s = op.index_b + 1;
                    new_s_found = true;
                }
            }
        }

        // 根据 Git 惯例调整纯粹添加或删除时的起始行号
        // 如果一个文件部分长度为0，其起始行号通常是变化发生处的前一行的行号；
        // 如果变化从文件最开始，则起始行号为0。
        if (old_l == 0 && new_l > 0) { // 纯粹添加内容到（可能为空的）旧文件位置
            if (hunk_start_in_ses == 0) { // 从 SES 最开始就是 INSERT (即文件开头插入)
                old_s = 0;
            } else {
                // 查找 Hunk 开始前的最后一个有效旧文件行号
                // (ses[hunk_start_in_ses-1] 应该是 Hunk 前的最后一个 MATCH 操作)
                int prev_op_old_idx = ses[hunk_start_in_ses - 1].index_a;
                old_s = (prev_op_old_idx != -1) ? prev_op_old_idx + 1 : 0;
            }
        } else if (!old_s_found && old_l > 0) { // Hunk 有旧内容，但没找到起始行（理论上不应发生）
            old_s = 1; // 极端的备用值
        }

        if (new_l == 0 && old_l > 0) { // 纯粹删除内容，新文件部分为空
            if (hunk_start_in_ses == 0) {
                new_s = 0;
            } else {
                int prev_op_new_idx = ses[hunk_start_in_ses - 1].index_b;
                new_s = (prev_op_new_idx != -1) ? prev_op_new_idx + 1 : 0;
            }
        } else if (!new_s_found && new_l > 0) {
            new_s = 1; // 极端的备用值
        }

        // 如果 Hunk 最终计算出的长度都为0 (例如，全是上下文但被错误地识别为Hunk，或SES有问题)
        // 则跳过打印这个 Hunk。
        if (old_l == 0 && new_l == 0 && hunk_start_in_ses < hunk_end_in_ses) {
             // This might happen if context lines were gathered but no actual changes within them.
             // Should be rare if first_change_idx logic is correct.
        } else {
            std::cout << "@@ -" << old_s << "," << old_l
                      << " +" << new_s << "," << new_l << " @@" << std::endl;

            // 6. 打印 Hunk 内容行
            for (int i = hunk_start_in_ses; i < hunk_end_in_ses; ++i) {
                const auto& op = ses[i];
                char prefix = ' ';
                switch (op.type) {
                    case EditType::MATCH:  prefix = ' '; break;
                    case EditType::INSERT: prefix = '+'; break;
                    case EditType::DELETE: prefix = '-'; break;
                }
                // op.line_content 是原始行，不需要额外加 std::endl，除非它本身不包含
                // 但 MyersDiffLines 提供的 line_content 应该是完整的行
                std::cout << prefix << op.line_content << std::endl;
            }
        }
        current_op_idx = hunk_end_in_ses; // 更新 SES 处理索引，准备下一个 Hunk
    }
}

bool is_path_under_or_equal(const std::filesystem::path& target_path, const std::filesystem::path& base_dir_spec) {
    if (target_path == base_dir_spec) { // 直接匹配文件本身
        return true;
    }

    // 检查 target_path 是否在 base_dir_spec 目录下
    auto base_it = base_dir_spec.begin();
    auto base_end = base_dir_spec.end();
    auto target_it = target_path.begin();
    auto target_end = target_path.end();

    // 跳过空的或 "." 的初始组件，这些可能由 lexical_normal 产生
    while (base_it != base_end && (base_it->empty() || *base_it == ".")) { ++base_it; }
    while (target_it != target_end && (target_it->empty() || *target_it == ".")) { ++target_it; }

    // 逐个比较路径组件
    while (base_it != base_end && target_it != target_end) {
        if (*base_it != *target_it) {
            return false; // 路径组件不匹配
        }
        ++base_it;
        ++target_it;
    }

    // 如果 base_dir_spec 的所有组件都被匹配完了 (base_it == base_end)，
    // 那么 target_path 就是 base_dir_spec 本身或者是其子路径。
    return base_it == base_end;
}


// --- Token 处理函数实现 ---

std::filesystem::path getRepositoryTokenFilePath(const std::filesystem::path& biogit_dir_path) {
    return biogit_dir_path / "biogit_token"; //
}

bool saveRepositoryToken(const std::filesystem::path& biogit_dir_path, const std::string& token) {
    if (biogit_dir_path.empty()) {
        std::cerr << "Utils::saveRepositoryToken Error: .biogit directory path is empty." << std::endl;
        return false;
    }
    std::filesystem::path token_file_path = getRepositoryTokenFilePath(biogit_dir_path);

    std::error_code ec;
    // 确保 .biogit 目录存在 (尽管调用此函数时它应该已经存在了)
    if (!std::filesystem::exists(biogit_dir_path, ec) || !std::filesystem::is_directory(biogit_dir_path, ec)) {
        std::cerr << "Utils::saveRepositoryToken Error: .biogit directory does not exist or is not a directory: "
                  << biogit_dir_path.string() << std::endl;
        if(ec) std::cerr << "  Filesystem error: " << ec.message() << std::endl;
        return false;
    }

    std::ofstream ofs(token_file_path, std::ios::trunc | std::ios::out);
    if (!ofs.is_open()) {
        std::cerr << "Utils::saveRepositoryToken Error: Could not open token file for writing: " << token_file_path.string() << std::endl;
        return false;
    }
    ofs << token; // 直接写入Token字符串
    ofs.close();

    if (!ofs.good()) {
        std::cerr << "Utils::saveRepositoryToken Error: Failed to write token to file: " << token_file_path.string() << std::endl;
        return false;
    }
    return true;
}

std::optional<std::string> loadRepositoryToken(const std::filesystem::path& biogit_dir_path) {
    if (biogit_dir_path.empty()) {
        return std::nullopt;
    }
    std::filesystem::path token_file_path = getRepositoryTokenFilePath(biogit_dir_path);

    if (!std::filesystem::exists(token_file_path)) {
        return std::nullopt; // 文件不存在
    }

    std::ifstream ifs(token_file_path);
    if (!ifs.is_open()) {
        std::cerr << "Utils::loadRepositoryToken Warning: Could not open token file for reading: " << token_file_path.string() << std::endl;
        return std::nullopt;
    }

    std::string token;
    // 读取整个文件内容作为Token
    if (std::getline(ifs, token)) {
        // 移除可能的尾部换行符
        if (!token.empty() && (token.back() == '\n' || token.back() == '\r')) token.pop_back();
        if (!token.empty() && token.back() == '\r') token.pop_back();
        ifs.close();
        return token;
    }
    ifs.close();
    return std::nullopt; // 文件为空或读取失败
}

bool clearRepositoryToken(const std::filesystem::path& biogit_dir_path) {
    if (biogit_dir_path.empty()) {
        return false;
    }
    std::filesystem::path token_file_path = getRepositoryTokenFilePath(biogit_dir_path);

    if (std::filesystem::exists(token_file_path)) {
        std::error_code ec;
        if (!std::filesystem::remove(token_file_path, ec)) {
            std::cerr << "Utils::clearRepositoryToken Error: Could not delete token file "
                      << token_file_path.string() << ": " << ec.message() << std::endl;
            return false;
        }
    }
    // 如果文件原本就不存在，也视为成功清除
    return true;
}


}
