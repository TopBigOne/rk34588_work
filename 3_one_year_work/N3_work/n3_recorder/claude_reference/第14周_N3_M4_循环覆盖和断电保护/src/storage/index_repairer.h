#pragma once

#include "storage/record_index.h"

// 启动时修索引（上次 kill -9 / 断电留下的）：
//   - 结束时间为 NULL：文件能读出帧 → 补结束时间（开始 + 实际时长）和大小；读不出帧 → 删文件和记录
//   - 文件不存在：删记录
// 返回修了几条；-1 = 出错
int repairUnfinishedRecordings(RecordIndex &recordIndex);
