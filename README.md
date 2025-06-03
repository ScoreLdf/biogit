# BioGit2: C++实现的分布式版本控制系统

BioGit2 是一个用 C++20 从头实现的分布式版本控制系统，旨在复现 Git 的核心功能与交互模式。它支持一套完整的本地版本控制流程，并且通过自定义的客户端-服务器架构实现了远程仓库的协同操作。

## 主要功能

* **核心 Git 数据模型**:
  * 实现了 Git 的基本对象类型：**Blob** (文件内容)、**Tree** (目录结构)、**Commit** (提交记录)。
  * 所有对象均使用 **SHA-1 哈希** 进行内容寻址和存储。
* **完备的本地操作**:
  * 仓库管理: `init`
  * 文件暂存与提交: `add`, `commit`, `status`, `rm`, `rm-cached`
  * 历史查看: `log`, `show` (可显示 blob, tree, commit, tag 对象内容)
  * 分支管理: `branch` (创建、列出、删除), `switch` (切换分支或commit)
  * 标签管理: `tag` (创建、列出、删除轻量标签)
  * 差异比较: `diff` (支持工作区 vs 索引区, 索引区 vs HEAD, commit vs commit, 并可指定路径)
  * 合并操作: `merge` (支持快进合并、三路合并及冲突解决流程)
* **客户端-服务器远程交互**:
  * 基于 **Boost.Asio** 实现的TCP/IP客户端-服务器通信架构。
  * 支持核心远程命令: `clone`, `fetch`, `push`, `pull`。
  * 远程仓库配置: `remote add`, `remote remove`, `remote -v`。
* **用户认证与安全**:
  * 客户端用户注册 (`register`) 和登录 (`login`) 功能。
  * 基于 **Token** 的远程操作认证机制，保障交互安全。
* **辅助功能**:
  * **异步日志系统**: 用于记录服务器和客户端操作详情。
  * **仓库配置管理**: 支持通过 `config` 命令管理本地仓库或全局配置 (部分实现)。
  * **命令行参数解析**: 支持类似 Git 的命令行接口。

## 技术栈

* **语言**: C++20
* **构建系统**: CMake
* **核心库**:
  * **Boost**: Asio (网络通信), Filesystem (文件操作), System
  * **Protobuf**: 用于数据序列化 (根据CMakeLists.txt)
  * **abseil-cpp (absl)**: Protobuf依赖及通用库
  * **jsoncpp**: JSON数据处理
  * **ZLIB**: 数据压缩 (根据CMakeLists.txt)
* **核心算法**:
  * **SHA-1**: 哈希计算 (自定义实现)
  * **Myers Diff**: 文件差异比较算法 (自定义实现)
* **网络**: TCP/IP

## 客户端-服务器架构概述

BioGit2 的远程功能采用客户端-服务器模型，主要模块包括：

* **AsioIOServicePool (I/O 服务池)**: 管理 `boost::asio::io_context` 池和工作线程，为网络操作提供异步执行环境。
* **CServer (服务器主控类)**: 监听端口，接受客户端连接，管理客户端会话 (`CSession`)。
* **CSession (客户端会话类)**: 代表与单个客户端的TCP连接，负责数据收发、协议解析，并将业务请求投递给 `LogicSystem`。
* **LogicSystem (业务逻辑处理核心)**: 解耦网络I/O与Git业务逻辑，通过消息队列和回调函数处理来自 `CSession` 的请求。
* **协议层 (biogit_protocol.h, SendNode, RecvNode)**: 定义客户端与服务器间的自定义二进制通信协议。
* **用户管理 (UserManager & TokenManager)**: 处理用户注册、登录及Token的生成与验证。

**交互流程简介**:

1.  客户端配置服务器地址和目标仓库。
2.  客户端与服务器建立TCP连接。
3.  服务器 `CServer` 接受连接，创建 `CSession` 实例。
4.  客户端发送 `MSG_REQ_TARGET_REPO` 指定远程仓库。
5.  `CSession` 处理仓库加载请求，并响应客户端。
6.  仓库选定后，客户端发送具体Git操作请求。
7.  `CSession` 解析消息，将请求投递给 `LogicSystem`。
8.  `LogicSystem` 根据消息ID分派给对应处理函数，处理函数通过 `CSession` 获取仓库实例执行操作。
9.  操作结果通过 `CSession` 发回客户端。

## 支持的命令概览

BioGit2 实现了一系列与 Git 兼容的命令，包括：

* **本地操作**: `init`, `add`, `commit`, `status`, `log`, `branch`, `switch`, `tag`, `merge`, `diff`, `rm`, `rm-cached`, `show`
* **配置命令**: `config` (`--list`, 获取/设置键值)
* **远程操作 (客户端)**: `clone`, `remote` (`add`, `remove`, `-v`), `fetch`, `push`, `pull`
* **用户认证 (客户端)**: `register`, `login`
* **服务器操作**: `server start` (启动BioGit2服务器)

详细用法可以通过 `./biogit2 help` 查看。

## 如何构建

```bash
git clone https://github.com/ScoreLdf/biogit.git
cd BioGit2
mkdir build && cd build
cmake ..
make
# 可执行文件 ./biogit2 将生成在 build 目录中
```
