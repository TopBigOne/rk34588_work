// 【扫一眼】接口只有一个虚函数，看一眼注释就够了
/*
 * StreamSource：输入接口 ——"一块一块提供码流"
 *
 * 策略模式：DecodePipeline 只认这个接口，不关心码流从哪来。
 *   - 现在：ReadStream（从 .h264 / .h265 文件按固定长度读）
 *   - 以后：RTSP 拉流、从网络收，实现同样的接口，DecodePipeline 和 MppDecoder 不用改
 *
 * 不要求按帧切：MppDecoder 打开了内部分帧（split_parse），给它任意长度的码流都行
 */
#pragma once

#include <cstdint> // uint8_t
#include <vector>

class StreamSource {
public:
    // 接口类的析构函数必须是 virtual：通过 StreamSource& 销毁对象时，才会调用到子类的析构函数
    virtual ~StreamSource() = default;

    /**
     * @brief 读下一块码流
     * @param data    输出参数：这一块的数据（读到多少字节，size() 就是多少；最后一块可能是 0 字节）
     * @param isLast  输出参数：true = 这是最后一块，后面没有了
     * @return  成功返回 true；读出错返回 false
     */
    virtual bool read(std::vector<uint8_t>& data, bool& isLast) = 0;
};
