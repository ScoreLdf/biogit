#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>   // C++17 文件系统库
#include <numeric>      // For std::accumulate (如果需要拼接字符串)
#include <algorithm>    // For std::find, std::all_of (检查字符串是否都是十六进制数字)
#include <memory>       // For std::shared_ptr
#include <condition_variable> // 用于服务器主循环的等待
#include <mutex>          // 用于服务器主循环的等待
#include <future>         // 用于服务器主循环的等待

#include "include/Repository.h"
#include "include/utils.h"
#include "include/RemoteClient.h"
#include "include/IoServicePool.h"
#include "include/LogicSystem.h"
#include "include/UserManager.h"
#include "include/AsyncLogger.h"
#include "include/protocol.h"

// --- 处理函数的向前声明 ---
void print_usage(); // 打印用法信息

// 本地仓库命令处理函数
void handle_init(const std::vector<std::string>& args);
void handle_add(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_commit(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_status(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_log(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_branch(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_switch(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_tag(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_diff(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_rm_cached(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_rm(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_show(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_merge(Biogit::Repository& repo, const std::vector<std::string>& args);

// 配置命令处理函数
void handle_config(Biogit::Repository* repo, const std::vector<std::string>& args); // repo 可以为 nullptr (例如全局配置)

// 客户端远程操作命令处理函数
void handle_clone(const std::vector<std::string>& args);
void handle_remote(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_fetch(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_push(Biogit::Repository& repo, const std::vector<std::string>& args);
void handle_pull(Biogit::Repository& repo, const std::vector<std::string>& args);

// 客户端用户认证命令处理函数
void handle_register_user(const std::vector<std::string>& args);
void handle_login_user(const std::vector<std::string>& args);

// 服务器命令处理函数
void handle_server_start(const std::vector<std::string>& args);

// 打印程序用法和支持的命令
void print_usage() {
    std::cout << "用法: biogit2 <命令> [<参数>...]" << std::endl; 
    std::cout << "\n常用的本地命令:" << std::endl; 
    std::cout << "  init [<路径>]               创建空 BioGit 仓库或重新初始化现有仓库" << std::endl; 
    std::cout << "  add <路径规则>...         将文件内容添加到索引区" << std::endl; 
    std::cout << "  status                    显示工作区状态" << std::endl; 
    std::cout << "  commit -m <消息>       记录变更到仓库" << std::endl; 
    std::cout << "  log                       显示提交日志" << std::endl; 
    std::cout << "  branch                    列出、创建或删除分支" << std::endl; 
    std::cout << "  branch <名称> [<起点>]   创建新分支" << std::endl; 
    std::cout << "  branch (-d | -D) <名称>   删除分支" << std::endl; 
    std::cout << "  switch <分支或提交>      切换分支或恢复工作区文件" << std::endl; 
    std::cout << "  tag                       列出、创建或删除标签" << std::endl; 
    std::cout << "  tag <名称> [<提交>]     创建新标签" << std::endl; 
    std::cout << "  tag -d <名称>             删除标签" << std::endl; 
    std::cout << "  diff [--staged] [<c1> <c2>] [<路径>...]" << std::endl; 
    std::cout << "                            显示提交之间、提交和工作区等之间的差异" << std::endl; 
    std::cout << "  rm <路径规则>...          从工作区和索引区移除文件" << std::endl; 
    std::cout << "  rm-cached <路径规则>...   从索引区移除文件" << std::endl; 
    std::cout << "  show <对象>             显示各种类型的对象 (blob, tree, commit, tag)" << std::endl; 
    std::cout << "  merge <分支或提交>      合并两个或多个开发历史" << std::endl; 

    std::cout << "\n配置:" << std::endl; 
    std::cout << "  config <键> [<值>]    获取和设置仓库或全局选项" << std::endl; 
    std::cout << "  config --list             列出所有配置变量" << std::endl; 

    std::cout << "\n远程仓库操作 (客户端):" << std::endl; 
    std::cout << "  clone <URL> [<目录>]       克隆仓库到新目录" << std::endl; 
    std::cout << "  remote add <名称> <URL>   添加远程仓库" << std::endl; 
    std::cout << "  remote remove <名称>      移除远程仓库" << std::endl; 
    std::cout << "  remote -v                 列出远程仓库及其URL" << std::endl; 
    std::cout << "  fetch <远程> [<引用>]    从其他仓库下载对象和引用" << std::endl; 
    std::cout << "  push <远程> <引用> [--force]" << std::endl; 
    std::cout << "                            更新远程引用及相关对象" << std::endl; 
    std::cout << "  pull <远程> <分支>    从其他仓库获取并集成" << std::endl; 

    std::cout << "\n用户认证 (客户端):" << std::endl; 
    std::cout << "  register <用户名> <密码>" << std::endl;
    std::cout << "                            在服务器上注册新用户" << std::endl; 
    std::cout << "  login <用户名> <密码>" << std::endl;
    std::cout << "                            登录到服务器并在当前仓库本地保存Token" << std::endl; 

    std::cout << "\n服务器操作:" << std::endl; 
    std::cout << "  server start <端口> <仓库根目录> <用户数据文件> <Token密钥> [<日志目录>] [<日志文件名前缀>]" << std::endl; 
    std::cout << "                            启动 BioGit 服务器" << std::endl; 
    std::cout << std::endl; 
}

// 辅助函数：解析 "host:port" 格式的服务器地址
bool parse_host_port(const std::string& addr_str, std::string& host, std::string& port) {
    size_t colon_pos = addr_str.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos == addr_str.length() - 1) {
        // 必须包含':'，且不能在开头或结尾
        return false;
    }
    host = addr_str.substr(0, colon_pos);
    port = addr_str.substr(colon_pos + 1);
    return true;
}

std::string server_addr_str = "localhost:10088";

int main(int argc, char* argv[]) {
    std::cerr.rdbuf(std::cout.rdbuf());

    if (argc < 2) { // 如果参数少于2个 (程序名 + 命令)，则打印用法并退出
        print_usage();
        return 1;
    }

    std::string command = argv[1]; // 第一个参数是命令
    std::vector<std::string> args; // 存储命令之后的所有参数
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    std::optional<Biogit::Repository> repo_opt; // 用于存储加载的仓库对象
    bool repo_needed = true;                    // 标记当前命令是否需要仓库上下文
    bool repo_must_exist = true;                // 标记仓库是否必须已存在 (例如 add, commit)

    // 特殊处理不需要仓库或创建仓库的命令
    if (command == "init" || command == "clone" || command == "server" ||
        command == "register" || command == "login" || command == "help" ) {
        repo_needed = false;
        repo_must_exist = false;
    }


    if (repo_needed) {
        // 尝试从当前路径或父路径找到 .biogit 仓库的根目录
        std::optional<std::filesystem::path> root_path = Biogit::Repository::find_repository_root(std::filesystem::current_path());
        if (!root_path) { // 如果没有找到仓库根目录
            if (repo_must_exist) { // 并且当前命令要求仓库必须存在
                 std::cerr << "fatal: not a biogit2 repository (or any of the parent directories): .biogit" << std::endl;
                 return 128; // Git 仓库不存在时的标准退出码
            }
        } else { // 如果找到了仓库根目录
            repo_opt = Biogit::Repository::load(*root_path); // 加载仓库
            if (!repo_opt) { // 如果加载失败
                std::cerr << "错误: 无法从路径 " << root_path->string() << " 加载仓库。" << std::endl;
                return 1;
            }
        }
    }

    // 命令分发和执行
    try {
        if (command == "init") {
            handle_init(args);
        } else if (command == "add") {
            if (!repo_opt) { std::cerr << "错误：'add' 命令未加载仓库。" << std::endl; return 128; }
            handle_add(*repo_opt, args);
        } else if (command == "commit") {
            if (!repo_opt) { std::cerr << "错误：'commit' 命令未加载仓库。" << std::endl; return 128; }
            handle_commit(*repo_opt, args);
        } else if (command == "status") {
            if (!repo_opt) { std::cerr << "错误：'status' 命令未加载仓库。" << std::endl; return 128; }
            handle_status(*repo_opt, args);
        } else if (command == "log") {
            if (!repo_opt) { std::cerr << "错误：'log' 命令未加载仓库。" << std::endl; return 128; }
            handle_log(*repo_opt, args);
        } else if (command == "branch") {
            if (!repo_opt) { std::cerr << "错误：'branch' 命令未加载仓库。" << std::endl; return 128; }
            handle_branch(*repo_opt, args);
        } else if (command == "switch") {
            if (!repo_opt) { std::cerr << "错误：'switch' 命令未加载仓库。" << std::endl; return 128; }
            handle_switch(*repo_opt, args);
        } else if (command == "tag") {
            if (!repo_opt) { std::cerr << "错误：'tag' 命令未加载仓库。" << std::endl; return 128; }
            handle_tag(*repo_opt, args);
        } else if (command == "diff") {
            if (!repo_opt) { std::cerr << "错误：'diff' 命令未加载仓库。" << std::endl; return 128; }
            handle_diff(*repo_opt, args);
        } else if (command == "rm") {
            if (!repo_opt) { std::cerr << "错误：'rm' 命令未加载仓库。" << std::endl; return 128; }
            handle_rm(*repo_opt, args);
        } else if (command == "rm-cached") {
            if (!repo_opt) { std::cerr << "错误：'rm-cached' 命令未加载仓库。" << std::endl; return 128; }
            handle_rm_cached(*repo_opt, args);
        } else if (command == "show"){
             if (!repo_opt) { std::cerr << "错误：'show' 命令未加载仓库。" << std::endl; return 128; }
             handle_show(*repo_opt, args);
        } else if (command == "merge"){
            if (!repo_opt) { std::cerr << "错误：'merge' 命令未加载仓库。" << std::endl; return 128; }
            handle_merge(*repo_opt, args);
        } else if (command == "config") {
            // config 命令可能在仓库内外执行 (例如 --global)，所以 repo_opt 可能为空
            handle_config(repo_opt.has_value() ? &(*repo_opt) : nullptr, args);
        } else if (command == "clone") {
            handle_clone(args);
        } else if (command == "remote") {
            if (!repo_opt) { std::cerr << "错误：'remote' 命令未加载仓库。" << std::endl; return 128; }
            handle_remote(*repo_opt, args);
        } else if (command == "fetch") {
            if (!repo_opt) { std::cerr << "错误：'fetch' 命令未加载仓库。" << std::endl; return 128; }
            handle_fetch(*repo_opt, args);
        } else if (command == "push") {
            if (!repo_opt) { std::cerr << "错误：'push' 命令未加载仓库。" << std::endl; return 128; }
            handle_push(*repo_opt, args);
        } else if (command == "pull") {
            if (!repo_opt) { std::cerr << "错误：'pull' 命令未加载仓库。" << std::endl; return 128; }
            handle_pull(*repo_opt, args);
        } else if (command == "register") { // 客户端注册命令
            handle_register_user(args);
        } else if (command == "login") {    // 客户端登录命令
            handle_login_user(args);
        } else if (command == "server") {   // 服务器相关命令
            handle_server_start(args);
        } else if (command == "help" || command == "--help" || command == "-h") { // 帮助命令
            print_usage();
        }
        else { // 未知命令
            std::cerr << "biogit2: '" << command << "' 不是一个 biogit2 命令。参见 'biogit2 help'。" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) { // 捕获标准异常
        std::cerr << "发生错误: " << e.what() << std::endl;
        return 1;
    }

    return 0; // 程序正常退出
}

// --- 处理函数的实现 ---

// 处理 'init' 命令
void handle_init(const std::vector<std::string>& args) {
    std::filesystem::path repo_path = "."; // 默认为当前目录
    if (!args.empty()) {
        repo_path = args[0];
    }
    auto repo = Biogit::Repository::init(repo_path);
}

// 处理 'add' 命令
void handle_add(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 add <路径规则>..." << std::endl;
        return;
    }
    for (const auto& path_str : args) {
        repo.add(path_str);
    }
}

// 处理 'commit' 命令
void handle_commit(Biogit::Repository& repo, const std::vector<std::string>& args) {
    std::string message;
    bool message_flag_found = false;
    // 解析 -m <message>
    if (args.size() >= 2 && args[0] == "-m") {
        message = args[1];
        // 如果消息包含空格，需要将后续参数拼接起来
        for(size_t i = 2; i < args.size(); ++i) {
            message += " " + args[i];
        }
        message_flag_found = true;
    }

    if (!message_flag_found || message.empty()) {
        std::cerr << "用法: biogit2 commit -m <消息>" << std::endl;
        return;
    }
    repo.commit(message);
}

void handle_status(Biogit::Repository& repo, const std::vector<std::string>& args) {
    repo.status();
}


void handle_log(Biogit::Repository& repo, const std::vector<std::string>& args) {
    repo.log();
}

// 处理 'branch' 命令
void handle_branch(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) { // 列出所有分支
        repo.branch_list();
    } else if (args.size() == 1) { // 创建分支: biogit2 branch <名称>
        repo.branch_create(args[0]);
    } else if (args.size() == 2) {
        if (args[0] == "-d" || args[0] == "-D") { // 删除分支: biogit2 branch -d <名称>
            repo.branch_delete(args[1], (args[0] == "-D")); //
        } else { // 从指定起点创建分支: biogit2 branch <名称> <起点>
            repo.branch_create(args[0], args[1]); //
        }
    } else { // 参数错误
        std::cerr << "用法: biogit2 branch" << std::endl;
        std::cerr << "   或: biogit2 branch <名称> [<起点>]" << std::endl;
        std::cerr << "   或: biogit2 branch (-d | -D) <名称>" << std::endl;
    }
}

// 处理 'switch' 命令
void handle_switch(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 switch <分支或提交>" << std::endl;
        return;
    }
    repo.switch_branch(args[0]); //
}

// 处理 'tag' 命令
void handle_tag(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) { // 列出所有标签
        repo.tag_list(); //
    } else if (args.size() == 1) { // 创建标签: biogit2 tag <名称> (指向 HEAD)
        repo.tag_create(args[0]); //
    } else if (args.size() == 2) {
        if (args[0] == "-d") { // 删除标签: biogit2 tag -d <名称>
            repo.tag_delete(args[1]); //
        } else { // 创建标签指向特定提交: biogit2 tag <名称> <提交>
            repo.tag_create(args[0], args[1]); //
        }
    } else { // 参数错误
        std::cerr << "用法: biogit2 tag" << std::endl;
        std::cerr << "   或: biogit2 tag <名称> [<提交标识>]" << std::endl;
        std::cerr << "   或: biogit2 tag -d <名称>" << std::endl;
    }
}

// 处理 'diff' 命令
void handle_diff(Biogit::Repository& repo, const std::vector<std::string>& args) {
    Biogit::DiffOptions options; // Diff 选项结构体
    std::vector<std::string> remaining_args = args; // 存储未被解析为选项的参数

    // 检查 --staged 选项
    auto staged_it = std::find(remaining_args.begin(), remaining_args.end(), "--staged");
    if (staged_it != remaining_args.end()) {
        options.staged = true;
        remaining_args.erase(staged_it); // 从剩余参数中移除 --staged
    }

    // 根据剩余参数数量判断 diff 类型
    if (remaining_args.size() >= 2 && !(remaining_args[0].rfind("-",0)==0) && !(remaining_args[1].rfind("-",0)==0) ) {
        // diff <commit1> <commit2> [<路径>...]
        // 假设前两个非选项参数是 commit 标识符
        options.commit1_hash_str = remaining_args[0];
        options.commit2_hash_str = remaining_args[1];
        for (size_t i = 2; i < remaining_args.size(); ++i) {
            options.paths_to_diff.push_back(remaining_args[i]); // 剩余的是路径
        }
    } else if (!remaining_args.empty()) {
        // diff <路径>...  (比较工作区和索引区)
        // 或 diff --staged <路径>... (比较索引区和HEAD)
        for (const auto& arg : remaining_args) {
            options.paths_to_diff.push_back(arg);
        }
    }

    repo.diff(options); //
}

// 处理 'rm' 命令 (从工作区和索引区移除)
void handle_rm(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 rm <文件>..." << std::endl;
        return;
    }
    for (const auto& path_str : args) {
        repo.rm(path_str);
    }
}

// 处理 'rm-cached' 命令 (仅从索引区移除)
void handle_rm_cached(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 rm-cached <文件>..." << std::endl;
        return;
    }
    for (const auto& path_str : args) {
        repo.rm_cached(path_str);
    }
}

// 处理 'show' 命令
void handle_show(Biogit::Repository& repo, const std::vector<std::string>& args){
    if(args.empty()){
        std::cerr << "用法: biogit2 show <对象哈希前缀>" << std::endl;
        return;
    }
    repo.show_object_by_hash(args[0]); //
}

// 处理 'merge' 命令
void handle_merge(Biogit::Repository& repo, const std::vector<std::string>& args){
    if(args.empty()){
        std::cerr << "用法: biogit2 merge <分支或提交>" << std::endl;
        return;
    }
    repo.merge(args[0]); //
}

// 处理 'config' 命令
void handle_config(Biogit::Repository* repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 config <键> [<值>] 或 biogit2 config --list" << std::endl;
        return;
    }

    if (args[0] == "--list") { // 列出所有配置
        std::map<std::string, std::string> configs;
        if (repo) { // 如果在仓库上下文中，获取本地配置
            configs = repo->config_get_all(); //
        } else {
            std::cerr << "错误: 在仓库上下文外部列出配置 (例如 --global) 暂未实现。" << std::endl;
             if(!repo) return; // 如果没有 repo 指针，则直接返回
        }
        for (const auto& pair : configs) {
            std::cout << pair.first << "=" << pair.second << std::endl;
        }
        return;
    }

    if (!repo) { // 对于非 --list 操作，如果不在仓库内，则报错 (除非将来支持 --global 写)
        std::cerr << "错误: 无法在仓库上下文外部获取或设置配置项 (除非使用 --global 选项，当前未支持)。" << std::endl;
        return;
    }

    if (args.size() == 1) { // 获取配置项: biogit2 config <键>
        auto value_opt = repo->config_get(args[0]); //
        if (value_opt) {
            std::cout << *value_opt << std::endl;
        } else {
            std::cerr << "配置键 '" << args[0] << "' 未找到。" << std::endl;
            exit(1); // 模拟 Git 的行为
        }
    } else if (args.size() == 2) { // 设置配置项: biogit2 config <键> <值>
        if (!repo->config_set(args[0], args[1])) { //
            std::cerr << "错误: 设置配置项 '" << args[0] << "' 失败。" << std::endl;
        }
    } else { // 参数错误
        std::cerr << "用法: biogit2 config <键> [<值>] 或 biogit2 config --list" << std::endl;
    }
}

// 处理 'clone' 命令
void handle_clone(const std::vector<std::string>& args) {
    if (args.size() < 1 || args.size() > 2) {
        std::cerr << "用法: biogit2 clone <仓库URL> [<目录>]" << std::endl;
        return;
    }
    std::string url = args[0];
    std::filesystem::path target_dir; // 目标目录
    if (args.size() == 2) {
        target_dir = args[1];
    } else { // 如果未指定目录，从 URL 推断
        size_t last_slash = url.find_last_of('/');
        if (last_slash != std::string::npos && last_slash < url.length() - 1) {
            target_dir = url.substr(last_slash + 1);
            // 移除可能的 .git 后缀
            std::string dirname_str = target_dir.string();
            if (dirname_str.length() > 4 && dirname_str.substr(dirname_str.length() - 4) == ".git") {
                target_dir = dirname_str.substr(0, dirname_str.length() - 4);
            }
        } else { // 无法从 URL 推断目录名
            std::cerr << "错误: 无法从 URL 推断目标目录名。请明确指定目录。" << std::endl;
            return;
        }
    }
    Biogit::Repository::clone(url, target_dir); //
}

// 处理 'remote' 命令
void handle_remote(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "-v") { // 列出远程仓库
        auto remotes = repo.remote_list_configs(); //
        for (const auto& pair : remotes) {
            std::cout << pair.first; // 远程名称
            if (!args.empty() && args[0] == "-v") { // 如果有 -v 选项，显示 URL
                 std::cout << "\t" << pair.second.url << " (push)\n";
                 std::cout << "\t" << pair.second.fetch_refspec << " (fetch)";
            }
            std::cout << std::endl;
        }
    } else if (args.size() >= 1 && args[0] == "add") { // 添加远程仓库
        if (args.size() != 3) { // biogit2 remote add <名称> <URL>
            std::cerr << "用法: biogit2 remote add <名称> <URL>" << std::endl;
            return;
        }
        repo.remote_add(args[1], args[2]); //
    } else if (args.size() >= 1 && args[0] == "remove") { // 移除远程仓库
        if (args.size() != 2) { // biogit2 remote remove <名称>
            std::cerr << "用法: biogit2 remote remove <名称>" << std::endl;
            return;
        }
        repo.remote_remove(args[1]); //
    } else { // 参数错误
        std::cerr << "用法: biogit2 remote [-v]" << std::endl;
        std::cerr << "   或: biogit2 remote add <名称> <URL>" << std::endl;
        std::cerr << "   或: biogit2 remote remove <名称>" << std::endl;
    }
}

// 处理 'fetch' 命令
void handle_fetch(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "用法: biogit2 fetch <远程名称> [<要获取的引用>]" << std::endl;
        return;
    }
    std::string remote_name = args[0];
    std::string ref_to_fetch; // 默认为空，表示获取所有
    if (args.size() > 1) {
        ref_to_fetch = args[1];
    }
    // 尝试从仓库的 .biogit 目录加载 Token
    std::optional<std::string> token_opt = Utils::loadRepositoryToken(repo.get_mygit_directory()); //
    if (!token_opt) { // 如果没有找到 Token
        std::cerr << "错误: 未登录。请先使用 'biogit2 login' 为此仓库登录。" << std::endl;
        return;
    }
    repo.fetch(remote_name, *token_opt, ref_to_fetch);
}

// 处理 'push' 命令
void handle_push(Biogit::Repository& repo, const std::vector<std::string>& args) {
    if (args.size() < 2) { // 至少需要 <远程名称> <本地引用>
        std::cerr << "用法: biogit2 push <远程名称> <本地引用>[:<远程引用名称>] [--force]" << std::endl;
        return;
    }
    std::string remote_name = args[0];
    std::string local_ref_spec = args[1]; // 可能的形式: local_branch 或 local_branch:remote_branch
    std::string local_ref = local_ref_spec;
    std::string remote_ref_on_server = local_ref_spec; // 默认推送到同名远程分支
    bool force = false;

    // 解析 local_ref:remote_ref 格式
    size_t colon_pos = local_ref_spec.find(':');
    if (colon_pos != std::string::npos) {
        local_ref = local_ref_spec.substr(0, colon_pos);
        remote_ref_on_server = local_ref_spec.substr(colon_pos + 1);
        if(remote_ref_on_server.empty()){ // 例如 "main:"
            std::cerr << "错误: 如果使用冒号指定远程引用，则远程引用名称不能为空。" << std::endl;
            return;
        }
    }

    // 检查 --force 选项
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--force") {
            force = true;
        } else if (remote_ref_on_server == local_ref && colon_pos == std::string::npos) {
             // 如果前面没有用冒号指定远程引用，且当前参数不是 --force，则将其视为远程引用名称
             // biogit2 push origin main feature -> local=main, remote=feature
             remote_ref_on_server = args[i];
        } else if (args[i] != "--force") { // 未知参数
            std::cerr << "错误: 未知参数 '" << args[i] << "'" << std::endl;
            std::cerr << "用法: biogit2 push <远程名称> <本地引用>[:<远程引用名称>] [--force]" << std::endl;
            return;
        }
    }

    std::optional<std::string> token_opt = Utils::loadRepositoryToken(repo.get_mygit_directory()); //
    if (!token_opt) {
        std::cerr << "错误: 未登录。请先使用 'biogit2 login' 为此仓库登录。" << std::endl;
        return;
    }
    // repo.push 应该处理推送逻辑并打印日志
    repo.push(remote_name, local_ref, remote_ref_on_server, force, *token_opt); //
}

// 处理 'pull' 命令
void handle_pull(Biogit::Repository& repo, const std::vector<std::string>& args) {
     if (args.size() < 1 || args.size() > 2) { // biogit2 pull <远程名称> [<远程分支名称>]
        std::cerr << "用法: biogit2 pull <远程名称> [<远程分支名称>]" << std::endl;
        return;
    }
    std::string remote_name = args[0];
    std::string remote_branch = "main"; // 如果未指定，默认拉取远程的 "main" 分支
    if(args.size() == 2){
        remote_branch = args[1];
    }

    std::optional<std::string> token_opt = Utils::loadRepositoryToken(repo.get_mygit_directory()); //
    if (!token_opt) {
        std::cerr << "错误: 未登录。请先使用 'biogit2 login' 为此仓库登录。" << std::endl;
        return;
    }

    repo.pull(remote_name, remote_branch, *token_opt); //
}

// 处理客户端 'register' 命令
void handle_register_user(const std::vector<std::string>& args) {
    std::string username, password;
    // 解析参数: biogit2 register <username> <password>
    if (args.size() == 2 ) {
        username = args[0];
        password = args[1];

    } else {
        std::cerr << "用法: biogit2 register <用户名> <密码>" << std::endl;
        return;
    }

    std::string host, port;
    if (!parse_host_port(server_addr_str, host, port)) { // 使用辅助函数解析服务器地址
        std::cerr << "错误: 服务器地址格式无效。应为 <主机:端口>。" << std::endl;
        return;
    }

    boost::asio::io_context io_ctx; // 为 RemoteClient 创建 io_context
    Biogit::RemoteClient client(io_ctx); //
    if (!client.Connect(host, port)) { // 连接服务器
        std::cerr << "错误: 无法连接到服务器 " << host << ":" << port << std::endl;
        return;
    }
    std::string server_message;
    // 调用 RemoteClient 的注册方法
    if (client.RegisterUser(username, password, server_message)) { //
        std::cout << "注册成功: " << server_message << std::endl;
    } else {
        std::cout << "注册失败: " << server_message << std::endl;
    }
    client.Disconnect(); // 断开连接
}

// 处理客户端 'login' 命令
void handle_login_user(const std::vector<std::string>& args) {
    std::string username, password;
    // 解析参数: biogit2 login <username> <password>
    if (args.size() == 2 ) {
        username = args[0];
        password = args[1];
    } else {
        std::cerr << "用法: biogit2 login <用户名> <密码>" << std::endl;
        return;
    }

    std::string host, port;
    if (!parse_host_port(server_addr_str, host, port)) {
        std::cerr << "错误: 服务器地址格式无效。应为 <主机:端口>。" << std::endl;
        return;
    }

    boost::asio::io_context io_ctx; // 为 RemoteClient 创建 io_context
    Biogit::RemoteClient client(io_ctx); //
    if (!client.Connect(host, port)) { // 连接服务器
        std::cerr << "错误: 无法连接到服务器 " << host << ":" << port << std::endl;
        return;
    }

    std::string token_received, server_message;
    // 调用 RemoteClient 的登录方法
    if (client.LoginUser(username, password, token_received, server_message)) { //
        std::cout << "登录成功。" << std::endl;
        // 尝试找到当前仓库的 .biogit 目录以保存 Token
        std::optional<std::filesystem::path> root_path_opt = Biogit::Repository::find_repository_root(std::filesystem::current_path());
        if (root_path_opt) { // 如果在仓库内
            std::filesystem::path biogit_dir = *root_path_opt / Biogit::Repository::MYGIT_DIR_NAME;
            if(Utils::saveRepositoryToken(biogit_dir, token_received)){ //
                 std::cout << "Token 已为此仓库保存。" << std::endl;
            } else { // 保存失败
                 std::cout << "警告: 无法为此仓库保存 Token。" << std::endl;
            }
        } else { // 不在仓库内
            std::cout << "提示: 当前不在 biogit2 仓库中。Token 未保存到本地仓库配置。" << std::endl;
            // std::cout << "收到的 Token: " << token_received << std::endl; // 可以考虑不直接显示 Token
        }
    } else { // 登录失败
        std::cout << "登录失败: " << server_message << std::endl;
    }
    client.Disconnect(); // 断开连接
}

// 处理 'server start' 命令
void handle_server_start(const std::vector<std::string>& args) {
    // biogit2 server start <port> <root_repo_dir> <token_secret> [<log_dir>] [<log_base_name>]
    using namespace Biogit;
    if (args.size() < 4 || args[0] != "start") {
        std::cerr << "用法: biogit2 server start <端口> <仓库根目录> <Token密钥> [<日志目录>] [<日志文件名前缀>]" << std::endl;
        return;
    }

    unsigned short port;
    try { // 将端口字符串转换为数字
        port = static_cast<unsigned short>(std::stoul(args[1]));
    } catch (const std::exception& e) {
        std::cerr << "错误: 无效的端口号 '" << args[1] << "'。" << std::endl;
        return;
    }

    std::filesystem::path root_repo_dir = args[2];    // 仓库根目录
    std::filesystem::path user_data_file = root_repo_dir / "user";
    std::string token_secret = args[3];               // Token 签名密钥

    std::filesystem::path log_dir = root_repo_dir / "logs"; // 默认日志目录
    if (args.size() > 4) {
        log_dir = args[4];
    }
    std::string log_base_name =  "biogit2_server"; // 默认日志文件名前缀
    if (args.size() > 5) {
        log_base_name = args[5];
    }

    // 1. 初始化日志记录器
    auto logger = Biogit::AsyncLogger::GetInstance(); //
    if (!logger->Start(log_dir, log_base_name, Biogit::LogLevel::INFO /* 或 DEBUG */)) {
        std::cerr << "严重错误: 无法启动异步日志记录器。服务器退出。" << std::endl;
        return;
    }
    BIOGIT_LOG_INFO("日志记录器已初始化。");

    // 2. 初始化用户管理器
    auto user_manager = Biogit::UserManager::GetInstance(); //
    user_manager->initialize(user_data_file); // 使用指定的用户数据文件路径初始化
    BIOGIT_LOG_INFO("用户管理器已初始化。用户数据文件: " + user_data_file.string());


    // 3. 初始化 Token 管理器
    auto token_manager = Biogit::TokenManager::GetInstance(); //
    token_manager->initialize(token_secret);
    BIOGIT_LOG_INFO("Token 管理器已初始化。");


    // 4. 初始化 IO 服务池
    auto io_service_pool = Biogit::AsioIOServicePool::GetInstance(); //
    BIOGIT_LOG_INFO("IO 服务池已初始化 (默认使用 " + std::to_string(std::thread::hardware_concurrency()) + " 个线程)。");

    // 5. 初始化逻辑系统
    auto logic_system = Biogit::LogicSystem::GetInstance(); //
    if(!logic_system->Start()){ // 启动逻辑系统的工作线程并注册回调
        BIOGIT_LOG_FATAL("无法启动逻辑系统。服务器退出。");
        logger->Stop();
        return;
    }
    BIOGIT_LOG_INFO("逻辑系统已初始化并启动。");


    // 6. 启动服务器
    try {
        Biogit::CServer server(*io_service_pool, port, *logic_system, root_repo_dir);
        BIOGIT_LOG_INFO("BioGit 服务器正在端口 " + std::to_string(port) + " 启动，服务于路径 " + root_repo_dir.string());
        std::cout << "BioGit 服务器已在端口 " << port << " 启动。按 Ctrl+C 退出。" << std::endl;
        BIOGIT_LOG_INFO("服务器主循环正在运行 (通过等待 Ctrl+C 模拟)。");

        // 使用一个简单的方法来阻塞主线程，保持主线程存活，以便服务器线程可以继续运行
        std::mutex mtx_server_lifetime;
        std::condition_variable cv_server_lifetime;
        std::unique_lock<std::mutex> lock_server_lifetime(mtx_server_lifetime);

        cv_server_lifetime.wait(lock_server_lifetime, []{ return false; });

    } catch (const std::exception& e) { // 捕获服务器启动过程中的异常
        BIOGIT_LOG_FATAL("服务器启动失败: " + std::string(e.what()));
        std::cerr << "服务器启动失败: " << e.what() << std::endl;
        logic_system->StopService();
        return;
    }

    BIOGIT_LOG_INFO("服务器正在关闭...");
    std::cout << "BioGit 服务器已停止。" << std::endl;
}