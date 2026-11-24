#pragma once

/**
 * @brief 4 路不同帧率 → 按主时钟对齐拼 2×2 → 编码成 H.265，写进 outputPath（实时速度，约 60 秒）
 * @param[in] outputPath  输出路径：普通文件（m5）或者命名管道 FIFO（m5screen）
 * @param[in] tag         日志标签，例如 "M5"
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 实现在 app/m5_aligned_mosaic.cpp。输入和帧率在 app_config.h 的 kM5ChannelInputs
 */
int runAlignedMosaicPipeline(const char *outputPath, const char *tag);

/**
 * @brief M5：输出到文件 kM5OutputPath，用来验收
 * @return 0 = 成功；1 = 失败
 * @note 就是 runAlignedMosaicPipeline(kM5OutputPath, "M5")
 */
int runM5AlignedMosaic();

/**
 * @brief M5 上屏：输出到 FIFO kM4FifoPath，用 run_m4_on_board_with_mac.sh m5screen 运行
 * @return 0 = 成功；1 = 失败
 * @note 就是 runAlignedMosaicPipeline(kM4FifoPath, "M5screen")
 */
int runM5AlignedMosaicOnScreen();
