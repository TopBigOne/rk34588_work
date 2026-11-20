#include "app/m4_show_mosaic.h"

#include "app/app_config.h"
#include "app/m3_encode_mosaic.h"

/**
 * @brief M4：拼图编码输出到 FIFO
 * @return 0 = 成功；1 = 失败
 * @note 和 M3 是同一条流水线（runMosaicEncodePipeline），只是输出路径从普通文件换成 FIFO。
 *       FIFO（命名管道，mkfifo 建出来的特殊文件）：一个进程往里写，另一个进程从里读，数据不落盘；
 *       fopen 以写方式打开 FIFO 时，如果还没有读的一方，会一直阻塞到对方打开为止。
 *       码流里没有时间戳，gst 拿到多少就显示多少，所以屏幕上可能是快放（M5 用主时钟解决）
 */
int runM4ShowMosaic() {
    return runMosaicEncodePipeline(kM4FifoPath, "M4");
}
