#ifndef __MPI_DEC_UTILS_H__
#define __MPI_DEC_UTILS_H__

#include <rockchip/rk_mpi.h> // RK_U32、MppBuffer、MppFrame 等类型；它自己也带了 extern "C"
#include <stdio.h>

#define SZ_1K (1024)
#define SZ_4K (SZ_1K * 4)

#define MAX_FILE_NAME_LENGTH 256
#define MPI_DEC_STREAM_SIZE (SZ_4K)
#define MPI_DEC_LOOP_COUNT 4

typedef enum MppDecBufMode_e {
    MPP_DEC_BUF_HALF_INT,
    MPP_DEC_BUF_INTERNAL,
    MPP_DEC_BUF_EXTERNAL,
    MPP_DEC_BUF_MODE_BUTT,
} MppDecBufMode;

#define msleep(x) usleep((x) * 1000)

#define MPP_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define MPP_BOOL_CAS __sync_bool_compare_and_swap

#define mpp_free(ptr) mpp_osal_free(__FUNCTION__, ptr)

#define MPP_FREE(ptr)                                                                                                  \
    do {                                                                                                               \
        if (ptr)                                                                                                       \
            mpp_free(ptr);                                                                                             \
        ptr = NULL;                                                                                                    \
    } while (0)

typedef void *FileReader; /* 文件读取器句柄（内部实现在 mpi_dec_utils.c） */
typedef void *DecBufMgr; /* 解码 buffer 管理器句柄 */

/*
 * FileBufSlot - reader 返回的码流数据槽
 *
 * reader_read() 每次返回一个 FileBufSlot，包含一包码流数据。
 * dec_simple 通过 data/size 直接访问数据；
 * dec_advanced 通过 buf（MppBuffer）做零拷贝传递。
 */
typedef struct FileBufSlot_t {
    RK_S32 index; /* 槽位索引（reader 内部管理的环形 buffer 编号） */
    MppBuffer buf; /* 码流数据对应的 MppBuffer（DMA buffer，advanced 模式用） */
    size_t size; /* 本包码流数据的有效字节数 */
    RK_U32 eos; /* End Of Stream 标志：1=文件已读完，这是最后一包 */
    char *data; /* 码流数据指针（simple 模式直接用这个读数据） */
} FileBufSlot;

/*
 * MpiDecTestCmd - 解码测试的命令行参数和运行时配置
 *
 * 由 main() 中 mpi_dec_test_cmd_init() 从命令行参数解析填充，
 * 然后传给 dec_decode() 驱动整个解码流程。
 */
typedef struct MpiDecTestCmd_t {
    char file_input[MAX_FILE_NAME_LENGTH]; /* -i: 输入码流文件路径 */
    char file_output[MAX_FILE_NAME_LENGTH]; /* -o: 输出 YUV 文件路径 */

    MppCodingType type; /* -t: 编码类型（7=H.264, 16777220=H.265 等） */
    MppFrameFormat format; /* -f: 输出帧格式（JPEG 模式可指定 YUV/RGB） */
    RK_U32 width; /* -w: 视频宽（JPEG 必须指定，H.264 可不指定） */
    RK_U32 height; /* -h: 视频高 */

    RK_U32 have_input; /* 是否指定了输入文件 */
    RK_U32 have_output; /* 是否指定了输出文件 */

    RK_U32 simple; /* 解码模式：1=simple（非JPEG），0=advanced（JPEG） */
    RK_S32 timeout; /* 超时时间 */
    RK_S32 frame_num; /* -n: 解码帧数（-1=无限循环, 0=到EOS, >0=指定帧数） */
    size_t pkt_size; /* 每次读取的码流块大小 */
    MppDecBufMode buf_mode; /* buffer 模式（内部/外部分配） */

    RK_S32 nthreads; /* 线程数（mpi_dec_multi_test 多路解码用） */
    size_t max_usage; /* 输出：帧 buffer 内存峰值使用量（字节） */

    FileReader reader; /* 文件读取器（内部管理读取和分包） */


    RK_U32 quiet; /* 静默模式：减少日志输出 */
    RK_U32 trace_fps; /* 是否追踪帧率 */
    char *file_slt; /* CRC 校验输出文件路径 */
} MpiDecTestCmd;

/*
 * 下面的函数在 mpi_dec_utils.c 里实现，按 C 编译。
 * 被 .cpp include 时要告诉 C++ 编译器"这些是 C 函数"，否则 C++ 会改写函数名（name mangling），链接时找不到。
 * 被 .c include 时 __cplusplus 没有定义，extern "C" 这几行不存在（C 编译器不认识这个语法）。
 */
#ifdef __cplusplus
extern "C" {
#endif

void mpi_dec_test_cmd_options(MpiDecTestCmd *cmd);

void reader_init(FileReader *reader, char *file_in, MppCodingType type,size_t buf_size);
void reader_deinit(FileReader reader);

void reader_start(FileReader reader);
void reader_sync(FileReader reader);
void reader_stop(FileReader reader);

size_t reader_size(FileReader reader);
MPP_RET reader_read(FileReader reader, FileBufSlot **buf);
MPP_RET reader_index_read(FileReader reader, RK_S32 index, FileBufSlot **buf);
void reader_rewind(FileReader reader);

MPP_RET dec_buf_mgr_init(DecBufMgr *mgr);
void dec_buf_mgr_deinit(DecBufMgr mgr);
MppBufferGroup dec_buf_mgr_setup(DecBufMgr mgr, RK_U32 size, RK_U32 count, MppDecBufMode mode);
void dump_mpp_frame_to_file(MppFrame frame, FILE *fp);

void mpp_osal_free(const char *caller, void *ptr);

#ifdef __cplusplus
}
#endif

#endif
