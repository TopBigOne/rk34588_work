#pragma once

/**
 * @brief M4：4 路解码 → 拼 2×2 → 编码成 H.265，写进命名管道 kM4FifoPath，由板子上的 gst-launch-1.0 读出来显示到屏幕
 * @return 0 = 成功；1 = 失败（已打印原因）
 * @note 实现在 app/m4_show_mosaic.cpp。必须先有人以“读”的方式打开 FIFO（gst 的 filesrc），
 *       否则程序会停在打开输出文件那一步等着。用 run_m4_on_board_with_mac.sh 运行，它会先起 gst
 */
int runM4ShowMosaic();
