#pragma once

/**
 * @brief 4 路解码 → RGA 拼 2×2 大图 → MPP 编码成 H.265，写进 outputPath
 * @param[in] outputPath  输出路径：普通文件（M3）或者命名管道 FIFO（M4）
 * @param[in] tag         日志标签，例如 "M3"
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 实现在 app/m3_encode_mosaic.cpp。M3、M4 都调用它，只是输出路径不同
 */
int runMosaicEncodePipeline(const char *outputPath, const char *tag);

/**
 * @brief M3：大图编码成 /userdata/av/mosaic.h265
 * @return 0 = 成功；1 = 失败
 * @note 就是 runMosaicEncodePipeline(kM3OutputPath, "M3")
 */
int runM3EncodeMosaic();
