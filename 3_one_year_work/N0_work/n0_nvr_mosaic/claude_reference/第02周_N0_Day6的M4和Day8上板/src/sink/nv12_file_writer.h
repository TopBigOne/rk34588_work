#pragma once

#include "common/mpp_utils.h"    // FilePtr
#include "common/nv12_picture.h"

/** 把 NV12 画面去掉 stride 填充，一帧接一帧写进文件（原始数据，没有文件头） */
class Nv12FileWriter {
public:
    /**
     * @brief 创建（或清空）输出文件
     * @param[in] filePath  文件路径，例如 "/userdata/av/mosaic_m2_snapshots.yuv"
     * @return true = 成功；false = 失败（已打印原因）
     * @note 文件已存在时会被清空
     */
    bool open(const char *filePath);

    /**
     * @brief 写一帧：只写画面部分，每帧 width × height × 3 / 2 字节
     * @param[in] picture  要写的画面
     * @return true = 写满一整帧；false = 写失败（已打印原因）
     * @note CPU 通过 mpp_buffer_get_ptr 读 buffer：读之前 sync_begin，让 CPU 看到 RGA 刚写进内存的新数据
     */
    bool writePicture(const Nv12Picture &picture);

    /**
     * @brief 一共写了几帧
     * @return 帧数
     * @note 用来验收
     */
    int getWrittenFrameCount() const { return writtenFrameCount; }

private:
    FilePtr filePtr;           // 输出文件，析构时自动 fclose
    int writtenFrameCount = 0; // 写了几帧
};
