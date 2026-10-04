/*
 * WriteStream：StreamSink 的实现 —— 把编码出来的码流（H.264 / H.265 裸流）写进输出文件，和 ReadYUV 一读一写
 *
 * H.264 裸流（Annex-B）就是把文件头（SPS/PPS）和每个 packet 原样首尾相接写进文件，不需要额外的文件头
 *
 * 用法：
 *   WriteStream streamWriter;
 *   if (!streamWriter.open(path)) return -1;
 *   streamWriter.write(header);          // 先写 SPS/PPS
 *   streamWriter.write(packet.data);     // 再一帧一帧写码流
 *   // 不用手动关文件：streamWriter 离开作用域时，FilePtr 自动 fclose（会把缓冲区里没写下去的数据写进文件）
 */
#pragma once

#include <cstdint> // uint8_t
#include <vector>

#include "common/mpp_utils.h" // FilePtr
#include "sink/stream_sink.h"

class WriteStream : public StreamSink {
public:
    /**
     * 创建输出文件
     * "wb"：文件不存在就新建，存在就清空；b = 二进制模式。所在目录必须存在
     * @param path  输出文件路径
     * @return  成功返回 true；打不开（比如目录不存在）打印原因，返回 false
     */
    bool open(const char* path);

    /**
     * 把一段码流完整写进文件
     * @param data  要写的码流（文件头或者一帧）
     * @return  全部写完返回 true；写不完（比如板子磁盘满了）返回 false，调用方用 perror 打印原因
     */
    bool write(const std::vector<uint8_t>& data) override;

private:
    FilePtr streamOutputFile_; // 输出文件（H.264 / H.265 裸流），析构时自动 fclose
};
