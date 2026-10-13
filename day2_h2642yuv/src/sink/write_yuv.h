// 【扫一眼】逐行 fwrite，M3 里自己写过
/*
 * WriteYuv : FrameSink —— 把解码出来的 NV12 图像写进文件
 *
 * 按 width × height 逐行写，跳过 stride 的填充（1920x1080 的帧在内存里是 1920x1088），
 * 所以文件里每帧正好 width × height × 3/2 字节，ffplay 用 -video_size 1920x1080 就能直接播
 */
#pragma once

#include "common/mpp_utils.h" // FilePtr
#include "sink/frame_sink.h"

class WriteYuv : public FrameSink {
public:
    /**
     * @brief 创建输出文件（"wb"：已存在就清空重写）
     * @param path  文件路径
     * @return  成功返回 true；打开失败打印原因，返回 false
     */
    bool open(const char* path);

    /**
     * @brief 写一帧 NV12：Y 平面 height 行 + UV 平面 height/2 行，每行只写 width 字节
     * @param frame  正常的图像帧（有 buffer）。只借用，不保存
     * @return 全部写成功返回 true；没有 buffer 或 fwrite 写不满返回 false
     */
    bool write(MppFrame frame) override;

private:
    FilePtr file_; // RAII：析构时 fclose（会把缓冲区里还没写下去的数据写进文件）
};
