#include "common/base64.h"

/**
 * @brief 一个 base64 字符对应的 6 位数值
 * @param[in] character  字符
 * @return 0～63；不是 base64 字符返回 -1
 * @note 字母表（RFC 4648 第 4 节）：A-Z = 0～25，a-z = 26～51，0-9 = 52～61，'+' = 62，'/' = 63
 */
static int getBase64Value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

/**
 * @brief base64 解码
 * @param[in] text  base64 文本
 * @return 字节；非法返回空
 * @note 做法：每读一个字符，把它的 6 位拼到 bitBuffer 的低位；攒够 8 位就取出最高的 8 位作为一个字节。
 *       第 1 步 遇到 '=' 就结束；第 2 步 普通字符拼进缓冲；第 3 步 够 8 位就输出一个字节
 */
std::vector<uint8_t> decodeBase64(const std::string &text) {
    std::vector<uint8_t> decodedBytes;
    uint32_t bitBuffer = 0;  // 还没输出的位，放在低位
    int bufferedBitCount = 0; // bitBuffer 里有几位有效
    for (char character : text) {
        // 第 1 步 '=' 是填充，后面没有数据了
        if (character == '=') {
            break;
        }
        // 第 2 步 拼进缓冲
        const int sixBitValue = getBase64Value(character);
        if (sixBitValue < 0) {
            return {}; // 非法字符：整个结果作废
        }
        bitBuffer = (bitBuffer << 6) | static_cast<uint32_t>(sixBitValue);
        bufferedBitCount += 6;
        // 第 3 步 够 8 位：右移 (bufferedBitCount - 8) 位，把最高的 8 位移到最低，& 0xFF 取出来
        if (bufferedBitCount >= 8) {
            bufferedBitCount -= 8;
            decodedBytes.push_back(static_cast<uint8_t>((bitBuffer >> bufferedBitCount) & 0xFF));
        }
    }
    // 剩下不足 8 位的是填充用的 0 位，丢掉
    return decodedBytes;
}
