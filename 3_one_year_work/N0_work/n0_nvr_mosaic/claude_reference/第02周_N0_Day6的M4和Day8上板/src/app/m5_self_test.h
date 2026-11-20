#pragma once

/**
 * @brief M5 自测：在板子上检查 MasterClock 和 FrameQueue::tryPopIfDue 的行为（用真的 MppFrame，不用解码器）
 * @return 0 = 全部通过；1 = 有一条没通过（已打印条件和实际值）
 * @note 实现在 app/m5_self_test.cpp。运行：./run_on_board_with_mac.sh m5test
 */
int runM5SelfTest();
