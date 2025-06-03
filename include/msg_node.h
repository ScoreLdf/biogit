#pragma once
#include "protocol.h"
#include <string>

namespace Biogit {

// 直接在 SendNode 和 RecvNode 中定义其数据成员
class MsgNode {
public:
    MsgNode() = default;
    virtual ~MsgNode() = default;
};

// SendNode 将负责将消息ID、消息体长度和消息体数据打包成一个连续的内存块，以备发送
class SendNode {
    SendNode(const SendNode&) = delete; // 禁止拷贝
    SendNode& operator=(const SendNode&) = delete; // 禁止赋值

    uint16_t _msg_id;              // 通信id
    uint32_t _body_length;         // 消息体的长度
    uint32_t _total_buffer_length; // 整个数据块的长度 (头部 + 消息体)
    char* _buffer;                 // 指向打包好的数据的指针
public:
    // 构造函数，用于发送 char* 类型的数据
    SendNode(const char* body_data, uint32_t body_length, uint16_t msg_id) : _msg_id(msg_id), _body_length(body_length) {
        _total_buffer_length = Protocol::HEAD_TOTAL_LEN + _body_length;
        _buffer = new char[_total_buffer_length];

        Protocol::pack_header(_buffer, _msg_id, _body_length); // 写入头部
        if (body_data && _body_length > 0) {
            std::memcpy(_buffer + Protocol::HEAD_TOTAL_LEN, body_data, _body_length); // 写入消息体
        }
    }

    // 构造函数，用于发送 std::string 类型的数据
    SendNode(const std::string& body_data_str, uint16_t msg_id): SendNode(body_data_str.c_str(), static_cast<uint32_t>(body_data_str.length()), msg_id) {}
    // 构造函数，用于发送 std::vector<char> 类型的数据
    SendNode(const std::vector<char>& body_data_vec, uint16_t msg_id) : SendNode(body_data_vec.data(), static_cast<uint32_t>(body_data_vec.size()), msg_id) {}
    // 构造函数，用于发送 std::vector<std::byte> 类型的数据
    SendNode(const std::vector<std::byte>& body_data_vec, uint16_t msg_id): SendNode(reinterpret_cast<const char*>(body_data_vec.data()), static_cast<uint32_t>(body_data_vec.size()), msg_id) {}
    // 析构是否buffer
    ~SendNode() {
        delete[] _buffer;
    }

    // 获取待发送数据块的信息
    const char* data() const {return _buffer;} // 获取待发送数据
    uint32_t total_length() const { return _total_buffer_length; } // 获取待发送总长度
    uint16_t get_msg_id() const { return _msg_id; }
    uint32_t get_body_length() const { return _body_length; }
};


// RecvNode 在 CSession 成功解析完消息头后被创建，用于存储即将到来的消息体
class RecvNode {
    RecvNode(const RecvNode&) = delete; // 禁止拷贝
    RecvNode& operator=(const RecvNode&) = delete; // 禁止赋值

    uint16_t _msg_id;
    uint32_t _expected_body_length; // 从消息头解析出的消息体总长度
    uint32_t _current_body_length;  // 当前已接收到的消息体长度
    char* _body_data;               // 存储消息体
public:
    // 构造函数在知道消息ID和消息体长度后调用
    RecvNode(uint16_t msg_id, uint32_t body_length): _msg_id(msg_id),_expected_body_length(body_length),_current_body_length(0) {
        if (_expected_body_length > 0) {
            _body_data = new char[_expected_body_length];
        } else {
            _body_data = nullptr;
        }
    }

    ~RecvNode() {
        delete[] _body_data;
    }


    /**
     *@brief 将接收到的数据追加到消息体缓冲区
     *@param data_chunk 待写入的数据
     *@param chunk_length 待写入数据的长度
     *@return 返回实际拷贝的字节数
     **/
    uint32_t append_data(const char* data_chunk, uint32_t chunk_length) {
        if (!_body_data || is_body_complete()) {
            return 0;
        }
        uint32_t space_left = _expected_body_length - _current_body_length;
        uint32_t len_to_copy = std::min(chunk_length, space_left);

        if (len_to_copy > 0) {
            std::memcpy(_body_data + _current_body_length, data_chunk, len_to_copy);
            _current_body_length += len_to_copy;
        }
        return len_to_copy;
    }


    // 获取已接收数据块的信息
    uint16_t get_msg_id() const { return _msg_id; }
    const char* get_body_data() const { return _body_data; }
    uint32_t get_current_body_length() const { return _current_body_length; } // 实际已接收的消息体长度
    uint32_t get_expected_body_length() const { return _expected_body_length; } // 期望的消息体总长度
    bool is_body_complete() const {return _current_body_length == _expected_body_length;} // 是否接受完整
};






}