/*
 * 用 RGA 往一张大图里贴画面：把一帧缩放后贴进指定的格子
 */
#pragma once

#include "common/nv12_picture.h"

/**
 * @brief 把 srcPicture 整张画面缩放到 dstPicture 里的 dstRect 这个格子，格子外面的像素不动
 * @param[in] srcPicture  源画面（例如解码器给的一帧），宽高、stride 用它自己的
 * @param[in] dstPicture  目标大图
 * @param[in] dstRect     贴到大图的哪个矩形；x、y、宽、高都必须是偶数
 * @return true = 成功；false = 失败（已打印原因）
 * @note 同步调用：返回时 RGA 已经读完 srcPicture、写完 dstPicture，srcPicture 的 buffer 可以马上还回去
 */
bool resizeNv12IntoRectByRga(const Nv12Picture &srcPicture, const Nv12Picture &dstPicture, const PictureRect &dstRect);

