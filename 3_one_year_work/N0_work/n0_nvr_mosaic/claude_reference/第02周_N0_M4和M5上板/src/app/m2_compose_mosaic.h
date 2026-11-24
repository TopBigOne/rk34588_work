#pragma once

/**
 * @brief M2：4 路解码 + RGA 拼成 2×2 大图。抽第 30、300 张大图存成 NV12 原始数据，在 Mac 上转成 PNG 看
 * @return 0 = 成功；1 = 任何一步失败（已打印原因）
 * @note 实现在 app/m2_compose_mosaic.cpp。输出 /userdata/av/mosaic_m2_snapshots.yuv（2 帧 1920×1080 NV12）
 */
int runM2ComposeMosaic();
