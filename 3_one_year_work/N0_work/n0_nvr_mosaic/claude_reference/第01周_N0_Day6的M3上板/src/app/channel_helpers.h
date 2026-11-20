/*
 * M2、M3 都要用的几件事：按 app_config.h 打开 4 路通道、出错时叫停所有通道、打印每一路的结果
 */
#pragma once

#include <memory> // std::unique_ptr
#include <vector>

#include "pipeline/decode_channel.h"
#include "pipeline/mosaic_compose_stage.h"

/**
 * @brief 按 kChannelInputs 建好每一路通道，并打开码流文件、初始化解码器
 * @param[out] channelPtrs  建好的通道，下标就是第几路
 * @return true = 全部打开成功；false = 有一路失败（已打印原因）
 * @note 还没起线程。失败时已经建好的通道留在 channelPtrs 里，随 vector 析构自动释放
 */
bool openAllChannels(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs);

/**
 * @brief 叫停所有通道：每一路的队列都 abort，把等在 push 里的解码线程叫醒
 * @param[in,out] channelPtrs  所有通道
 * @return 无
 * @note 拼图、编码出错时调用。解码线程醒来后 push 返回 false，自己退出
 */
void abortAllChannels(std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs);

/**
 * @brief 打印每一路的结果：解出几帧、错误帧几帧、贴了几帧
 * @param[in] channelPtrs   所有通道
 * @param[in] composeStage  拼图段，从它读每一路贴了几帧
 * @param[in] tag           日志标签，例如 "M2"
 * @return true = 每一路都正常解完、没有错误帧、解出的帧全部贴上了；false = 有一路不满足
 * @note 所有线程 join 之后再调用
 */
bool printChannelResults(const std::vector<std::unique_ptr<DecodeChannel>> &channelPtrs,
                         const MosaicComposeStage &composeStage, const char *tag);
