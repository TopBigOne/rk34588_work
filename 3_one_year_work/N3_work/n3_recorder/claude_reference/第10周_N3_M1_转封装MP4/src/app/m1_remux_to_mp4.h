#pragma once

// M1：一路码流（文件或 rtsp://）→ 一个 MP4。返回 0 = 成功
int runM1RemuxToMp4(const char *inputUrl, const char *outputPath);
