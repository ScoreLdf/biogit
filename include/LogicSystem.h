#include <map>
#include <thread>

#include"Singleton.h"


namespace Biogit {
class Csession;
class LogicNode;

class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;     // 允许 Singleton 模板访问其私有构造函数
    using FunCallBack = std::function<void(std::shared_ptr<Csession>, uint16_t, const char*, uint32_t)>; // 封装统一回调函数签名：接收 CSession 的共享指针, 消息ID, 消息体数据指针, 消息体长度
public:
    ~LogicSystem();

    /**
     * @brief 启动 LogicSystem 的工作线程并注册回调。
     * @return 如果成功启动则为 true，否则为 false。
     */
    bool Start();

    /**
     * @brief 请求停止 LogicSystem 的服务。
     * 这会设置停止标志并通知工作线程，工作线程会在处理完当前消息队列后退出。
     */
    void StopService();

    /**
     * @brief 将一个已接收并封装好的消息节点投递到处理队列中。此方法是线程安全的。
     * @param msg 指向 LogicNode 的共享指针。
     */
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);

private:
    LogicSystem();

    /**
     * @brief 工作线程的主循环函数。会持续从消息队列中取出消息并分发给相应的回调函数处理。
     */
    void DealMsg();

    /**
     * @brief 注册所有已知的消息ID及其对应的处理回调函数。在 Start() 方法中被调用。
     */
    void RegisterCallBacks();

    // --- biogit2 协议处理回调函数 ---
    //  MSG_REQ_TARGET_REPO 是在 CSession 中直接处理的，LogicSystem 不直接处理它
    void HandleReqListRefs(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqGetObject(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqCheckObjects(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqPutObject(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqUpdateRef(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqRegisterUser(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);
    void HandleReqLoginUser(std::shared_ptr<Csession> session, uint16_t msg_id, const char* body_data, uint32_t body_length);

    // 辅助函数, 处理Token
    std::tuple<std::string, const char*, uint32_t> extractTokenAndPayload(const char* body_data_with_token, uint32_t body_length_with_token);
    bool authenticateAndPreparePayload(
        std::shared_ptr<Csession> session, // 当前会话
        const char* body_data_with_token, // 包含Token前缀的完整消息体
        uint32_t body_length_with_token, // 完整消息体的长度
        const std::string& handler_name_for_logging, // 用于日志记录的当前处理函数名
        const char*& out_original_body_ptr,     // 指向原始消息体数据的指针 (Token之后的部分)
        uint32_t& out_original_body_len,       // 原始消息体的长度
        std::string& out_username_from_token); // 从有效Token中解析出的用户名


    std::thread _worker_thread;                           // 执行业务逻辑的工作线程
    std::queue<std::shared_ptr<LogicNode>> _msg_que;      // 消息队列，存储待处理的 LogicNode
    std::mutex _mutex;                                    // 互斥锁，用于保护对消息队列的并发访问
    std::condition_variable _consume;                     // 条件变量，用于在队列为空时让工作线程等待，有新消息时唤醒
    std::atomic<bool> _b_stop;                            // 原子类型的停止标志，用于线程安全地通知工作线程退出
    std::map<uint16_t, FunCallBack> _fun_callbacks;       // 存储消息ID到其处理回调函数的映射
};


}



