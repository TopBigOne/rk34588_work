//
// Created by DEV on 2026/11/9.
//
#pragma once

/**
 * @brief M1 第 1 步：只跑解码段（读文件 → MPP 解码），数帧数，不缩放、不编码
 * @return 0 = 解到 EOS 并且没有错误帧；1 = 打开失败、解码出错，或者有错误帧（已打印原因）
 * @note 实现在 app/m1_decode_only_check.cpp。验收：正常帧 1803、错误帧 0、第 1 帧 stride 1920×1088
 */
int runM1DecodeOnlyCheck();
