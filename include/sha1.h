#pragma once

#include <string>
namespace SHA1 {
    using std::string;

    /**
     * @brief 计算给定字节数据的 SHA-1 哈希值。
     * @param data 要计算哈希的字节向量。
     * @return 40 个字符的十六进制 SHA-1 哈希字符串。
     */
    std::string sha1(const std::vector<std::byte>& data);

    /**
     * @brief 计算给定文本字符串的 SHA-1 哈希值。
     * @param text_data 要计算哈希的文本字符串，内部会将文本转换为字节序列。
     * @return 40 个字符的十六进制 SHA-1 哈希字符串。
     */
    std::string sha1(const std::string& text_data);


    // 可变参数版本，使用 string 相加
    template<typename ... Args>
    string sha1(const Args &...args) {
        std::string combined = (std::string{} + ... + args);
        return sha1(combined);
    }

};
