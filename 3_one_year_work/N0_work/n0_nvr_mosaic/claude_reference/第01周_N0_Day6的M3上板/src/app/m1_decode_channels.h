#pragma once

/**
 * @brief M1：4 路解码同时跑。每路一个线程、一个 DecodeStage（一个 MppCtx），只解码、数帧数
 * @return 0 = 4 路都解到 EOS，而且没有错误帧；1 = 有一路打开失败、解码出错，或者有错误帧（已打印原因）
 * @note 实现在 app/m1_decode_channels.cpp
 */
int runM1DecodeChannels();
