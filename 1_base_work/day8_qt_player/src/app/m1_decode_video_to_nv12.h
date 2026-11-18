#pragma once

/**
 * @brief M1：mp4 → 拆封装 → Annex B → MPP 硬解 → NV12 文件（去掉 stride 填充，紧凑排列）
 * @param[in] inputPath       mp4 文件路径
 * @param[in] outputPath      输出的 .nv12 文件路径
 * @param[in] maxFrameCount   最多解几帧；≤ 0 = 全部
 * @return 0 = 成功；1 = 失败
 * @note 不显示、不按时间控速，解多快算多快。输出可以和 Mac 上
 *       ffmpeg -i in.mp4 -pix_fmt nv12 -f rawvideo out.nv12 的结果比 md5，验证硬解结果逐位正确
 */
int runM1DecodeVideoToNv12(const char *inputPath, const char *outputPath, int maxFrameCount);
