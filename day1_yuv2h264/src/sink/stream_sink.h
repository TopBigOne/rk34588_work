/*
 * StreamSink：输出接口 ——"接收编码好的码流"
 *
 * 策略模式：EncodePipeline 只认这个接口，不关心码流写到哪。
 *   - 现在：WriteStream（写进 .h264 / .h265 文件）
 *   - 以后：推流（RTSP）、发到网络，实现同样的接口，EncodePipeline 不用改
 */
#pragma once

#include <cstdint> // uint8_t
#include <vector>

class StreamSink {
public:
    // 接口类的析构函数必须是 virtual：通过 StreamSink& 销毁对象时，才会调用到子类的析构函数
    virtual ~StreamSink() = default;

    /**
     * 输出一段码流（文件头或者一帧）
     * @param data  码流数据
     * @return  全部输出成功返回 true；失败返回 false
     */
    virtual bool write(const std::vector<uint8_t>& data) = 0;
};
