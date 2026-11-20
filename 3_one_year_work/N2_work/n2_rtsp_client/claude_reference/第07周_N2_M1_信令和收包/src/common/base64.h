/*
 * base64 解码：SDP 里的 sprop-parameter-sets（SPS、PPS）是 base64 文本，要还原成字节。
 * N1 只有编码（生成 SDP），客户端要的是解码。
 */
#pragma once

#include <cstdint> // uint8_t
#include <string>
#include <vector>

/**
 * @brief 把 base64 文本解码成字节
 * @param[in] text  base64 文本，例如 "Z0LAKNkA"；末尾的 '=' 可有可无（有的摄像头不写）
 * @return 解出的字节；遇到非法字符（不在 A-Z a-z 0-9 + / = 里）返回空 vector
 * @note base64 每 4 个字符表示 3 个字节（每个字符 6 位，4 × 6 = 24 位 = 3 字节）。
 *       '=' 是填充：最后一组不满 3 字节时补的，"xx==" 表示这组只有 1 个字节，"xxx=" 表示 2 个
 */
std::vector<uint8_t> decodeBase64(const std::string &text);
