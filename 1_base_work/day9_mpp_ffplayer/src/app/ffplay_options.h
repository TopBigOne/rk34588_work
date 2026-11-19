#pragma once

/**
 * @file
 * @brief 命令行选项表和帮助信息（parse_options 用）
 * @note 选项的值写到 common/player_options.h 里的全局变量
 */

#include "common/ffplay_common.h"

/** ffplay 的全部命令行选项，以 { NULL } 结尾 */
extern const OptionDef options[];

/**
 * @brief parse_options 遇到不带 "-" 的参数时调用，记为输入文件
 * @note 只能有一个输入文件，"-" 表示标准输入
 */
void opt_input_file(void *optctx, const char *filename);

/**
 * @brief 打印一行用法说明
 */
void show_usage(void);
