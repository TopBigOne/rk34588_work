#pragma once

// M2：码流（H.264 文件或 rtsp://）→ 接收线程：码流进事件录像 + 送解码 → LatestFrameSlot
//     → 检测线程：检测 → 在帧上画 person 框 → 更新事件状态机
// 文件输入按帧率实时放（和摄像头一样的节奏）；loopCount > 1 时文件循环放几遍。Ctrl+C 正常收尾。
// 参数传 nullptr / 0 用 app_config.h 的默认值。
int runM2EventRecord(const char *requestedInputUrl, const char *requestedModelPath, int requestedLoopCount);
