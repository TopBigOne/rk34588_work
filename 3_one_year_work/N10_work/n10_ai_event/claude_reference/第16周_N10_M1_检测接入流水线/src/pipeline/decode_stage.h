// 来源：3_one_year_work/N0_work/n0_nvr_mosaic/claude_reference/第02周_N0_Day6的M4和Day8上板/src/pipeline/decode_stage.h
#pragma once

#include <cstdint> // int64_t、uint8_t
#include <string>
#include <vector>

#include "common/mpp_utils.h"
#include "decoder/mpp_decoder.h"
#include "source/stream_file_reader.h"

/** readNextFrame() 的结果 */
enum class ReadFrameResult {
    Frame,       // 交出一帧正常画面
    EndOfStream, // 解完了，不会再有帧
    Error,       // 出错
};

/**
 * @brief 解码段：读码流文件 → MPP 解码，每次交出一帧正常画面
 * @note 一路一个对象。多路同时跑时，每个对象只在自己的线程里用，对象之间不共享任何东西
 */
class DecodeStage {
public:
    /**
     * @brief 打开码流文件，初始化解码器
     * @param[in] streamPath        码流文件路径
     * @param[in] codingType        码流格式：MPP_VIDEO_CodingAVC / MPP_VIDEO_CodingHEVC
     * @param[in] requestedLogName  日志名字，例如 "ch0"
     * @return true = 成功；false = 失败（已打印原因）
     * @note 只能调用一次
     */
    bool open(const char *streamPath, MppCodingType codingType, const char *requestedLogName);

    /**
     * @brief 让解码段给每一帧打上时间戳（M5 起）：第 i 个正常帧的时间戳 = i × 1000000 / framesPerSecond 微秒
     * @param[in] framesPerSecond  这一路的帧率，例如 15、30、60；0 = 不打时间戳（默认，M1～M4 就是这样）
     * @return 无
     * @note 在 open 之后、第一次 readNextFrame 之前调用。裸码流文件里没有可靠的时间戳，只能按帧率自己算
     */
    void setPtsFrameRate(int framesPerSecond) { ptsFrameRate = framesPerSecond; }

    /**
     * @brief 交出下一帧正常画面：没有帧就送码流，直到解出一帧、解完或出错
     * @param[out] decodedFramePtr  解出的帧，只有返回 Frame 时有效；它带着解码器内存池里的一块 buffer
     * @return Frame = 一帧正常画面；EndOfStream = 解完了；Error = 出错（已打印原因）
     * @note 错误帧（errinfo / discard 不为 0）在内部跳过并计数，不交出去
     */
    ReadFrameResult readNextFrame(MppFramePtr &decodedFramePtr);

    /**
     * @brief 交出去的正常帧数
     * @return 帧数
     * @note 验收：应该等于 ffprobe 数出来的帧数
     */
    int getDecodedFrameCount() const { return decodedFrameCount; }

    /**
     * @brief 跳过的错误帧数
     * @return 帧数；正常的文件是 0
     * @note 用来验收
     */
    int getErrorFrameCount() const { return errorFrameCount; }

    /**
     * @brief 从文件读了多少字节
     * @return 字节数；解完时等于文件大小
     * @note 用来验收
     */
    int64_t getInputBytes() const { return reader.getReadBytes(); }

private:
    std::string logName;              // 日志名字
    StreamFileReader reader;          // 读码流文件
    MppDecoder decoder;               // MPP 解码器
    std::vector<uint8_t> pendingChunk; // 读出来、还没送进解码器的一块码流
    bool hasPendingChunk = false;     // pendingChunk 里有没有还没送进去的数据
    bool isInputEos = false;          // 文件读完了，最后一块已经读出来
    bool isOutputEos = false;         // 解码器已经吐出带 EOS 的帧
    int decodedFrameCount = 0;        // 交出去的正常帧数
    int errorFrameCount = 0;          // 跳过的错误帧数
    int ptsFrameRate = 0;             // 按这个帧率打时间戳；0 = 不打（M5 起）
};
