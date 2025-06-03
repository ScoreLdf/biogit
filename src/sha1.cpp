#include "../include/sha1.h"
#include <iostream>
#include <vector>
#include <cstdint>  // 添加uint32_t支持
namespace SHA1 {
    // 循环左移函数
    inline uint32_t S(uint32_t x, int n) {
        return (x << n) | (x >> (32 - n));
    }

    // SHA-1 逻辑函数 f_t
    inline uint32_t f_t(int t, uint32_t b, uint32_t c, uint32_t d) {
        if (t < 20)
            return (b & c) | ((~b) & d);
        else if (t < 40)
            return b ^ c ^ d;
        else if (t < 60)
            return (b & c) | (b & d) | (c & d);
        else // t < 80
            return b ^ c ^ d;
    }

    inline uint32_t K_t(int t) {
        if (t < 20)
            return 0x5a827999;
        else if (t < 40)
            return 0x6ed9eba1;
        else if (t < 60)
            return 0x8f1bbcdc;
        else
            return 0xca62c1d6;
    }

    // 核心SHA-1计算逻辑
    std::array<uint32_t, 5> process_data(const std::vector<std::byte>& original_data) {
        uint32_t H0 = 0x67452301;
        uint32_t H1 = 0xefcdab89;
        uint32_t H2 = 0x98badcfe;
        uint32_t H3 = 0x10325476;
        uint32_t H4 = 0xc3d2e1f0;

        std::vector<std::byte> padded_data = original_data;
        uint64_t original_length_bits = static_cast<uint64_t>(original_data.size()) * 8;

        padded_data.push_back(static_cast<std::byte>(0x80));

        while (padded_data.size() % 64 != 56) {
            padded_data.push_back(static_cast<std::byte>(0x00));
        }

        for (int i = 0; i < 8; ++i) {
            padded_data.push_back(static_cast<std::byte>((original_length_bits >> (56 - 8 * i)) & 0xFF));
        }

        int num_chunks = padded_data.size() / 64;
        uint32_t W[80];

        for (int i = 0; i < num_chunks; ++i) {
            const std::byte* chunk_ptr = padded_data.data() + (i * 64);

            for (int j = 0; j < 16; ++j) {
                W[j] = (static_cast<uint32_t>(chunk_ptr[j * 4 + 0]) << 24) |
                       (static_cast<uint32_t>(chunk_ptr[j * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(chunk_ptr[j * 4 + 2]) << 8)  |
                       (static_cast<uint32_t>(chunk_ptr[j * 4 + 3]) << 0);
            }

            for (int j = 16; j < 80; ++j) {
                W[j] = S(W[j - 3] ^ W[j - 8] ^ W[j - 14] ^ W[j - 16], 1);
            }

            uint32_t A = H0;
            uint32_t B = H1;
            uint32_t C = H2;
            uint32_t D = H3;
            uint32_t E = H4;

            for (int t = 0; t < 80; ++t) {
                uint32_t temp = S(A, 5) + f_t(t, B, C, D) + E + K_t(t) + W[t];
                E = D;
                D = C;
                C = S(B, 30);
                B = A;
                A = temp;
            }

            H0 += A;
            H1 += B;
            H2 += C;
            H3 += D;
            H4 += E;
        }
        return {H0, H1, H2, H3, H4};
    }

    std::string sha1(const std::vector<std::byte>& data) {
        std::array<uint32_t, 5> hash_components = process_data(data);
        char hex_str[41];
        std::sprintf(hex_str, "%08x%08x%08x%08x%08x",
                     hash_components[0], hash_components[1], hash_components[2],
                     hash_components[3], hash_components[4]);
        return std::string(hex_str);
    }

    std::string sha1(const std::string& text_data) {
        std::vector<std::byte> byte_data(text_data.length());
        // 更安全的是显式转换每个字符
        std::transform(text_data.begin(), text_data.end(), byte_data.begin(), [](char c) {
            return static_cast<std::byte>(c);
        });
        return sha1(byte_data); // 调用处理 vector<byte> 的版本
    }

};