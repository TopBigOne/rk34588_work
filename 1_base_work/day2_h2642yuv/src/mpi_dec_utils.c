/*
 * Copyright 2020 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define MODULE_TAG "mpi_dec_utils"

#include <pthread.h>
#include <rockchip/rk_mpi.h>
#include <string.h>

#include "mpi_dec_utils.h"

#include <stdlib.h>
#include <unistd.h>

#define IVF_HEADER_LENGTH 32
#define IVF_FRAME_HEADER_LENGTH 12

#define DEFAULT_PACKET_SIZE SZ_4K

typedef enum {
    FILE_NORMAL_TYPE,
    FILE_JPEG_TYPE,
    FILE_IVF_TYPE,
    FILE_BUTT,
} FileType;

typedef FileBufSlot *(*ReaderFunc)(FileReader data);

typedef struct FileReader_t {
    FILE *fp_input;
    size_t file_size;

    MppCodingType type;
    FileType file_type;
    char *buf;
    size_t buf_size;
    size_t stuff_size;
    RK_S32 seek_base;
    ReaderFunc read_func;

    /* return value for each read */
    size_t read_total;
    size_t read_size;
    MppBufferGroup group;

    pthread_t thd;
    volatile RK_U32 thd_stop;

    RK_U32 slot_max;
    RK_U32 slot_cnt;
    RK_U32 slot_rd_idx;
    FileBufSlot **slots;
} FileReaderImpl;

typedef struct DecBufMgrImpl_t {
    MppDecBufMode buf_mode;
    RK_U32 buf_count;
    RK_U32 buf_size;
    MppBufferGroup group;
    MppBuffer *bufs;
} DecBufMgrImpl;

#define READ_ONCE(var) (*((volatile typeof(var) *) (&(var))))


static void rearrange_pix(RK_U8 *tmp_line, RK_U8 *base, RK_U32 n) {
    RK_U16 *pix = (RK_U16 *) (tmp_line + n * 16);
    RK_U16 *base_u16 = (RK_U16 *) (base + n * 10);

    pix[0] = base_u16[0] & 0x03FF;
    pix[1] = (base_u16[0] & 0xFC00) >> 10 | (base_u16[1] & 0x000F) << 6;
    pix[2] = (base_u16[1] & 0x3FF0) >> 4;
    pix[3] = (base_u16[1] & 0xC000) >> 14 | (base_u16[2] & 0x00FF) << 2;
    pix[4] = (base_u16[2] & 0xFF00) >> 8 | (base_u16[3] & 0x0003) << 8;
    pix[5] = (base_u16[3] & 0x0FFC) >> 2;
    pix[6] = (base_u16[3] & 0xF000) >> 12 | (base_u16[4] & 0x003F) << 4;
    pix[7] = (base_u16[4] & 0xFFC0) >> 6;
}


static MPP_RET add_new_slot(FileReaderImpl *impl, FileBufSlot *slot) {
    // mpp_assert(impl);

    slot->index = impl->slot_cnt;
    impl->slots[impl->slot_cnt] = slot;
    impl->slot_cnt++;

    if (impl->slot_cnt >= impl->slot_max) {
        impl->slots = realloc(impl->slots, impl->slot_max * 2 * sizeof(FileBufSlot *));
        if (!impl->slots)
            return MPP_NOK;

        impl->slot_max *= 2;
    }

    // mpp_assert(impl->slots);
    // mpp_assert(impl->slot_cnt < impl->slot_max);

    return MPP_OK;
}


static FileBufSlot *read_normal_file(FileReader data) {
    FileReaderImpl *impl = (FileReaderImpl *) data;
    FILE *fp = impl->fp_input;
    size_t read_size = 0;
    size_t buf_size = impl->buf_size;
    size_t size = sizeof(FileBufSlot) + buf_size + impl->stuff_size;
    FileBufSlot *slot = (FileBufSlot *) malloc(size);
    RK_U32 eos = 0;

    slot->data = (char *) (slot + 1);
    read_size = fread(slot->data, 1, buf_size, fp);
    impl->read_total += read_size;
    impl->read_size = read_size;

    /* check reach eos whether or not */
    if (read_size != buf_size || feof(fp) || impl->read_total >= impl->file_size)
        eos = 1;

    slot->buf = NULL;
    slot->size = read_size;
    slot->eos = eos;

    return slot;
}
size_t reader_size(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;
    size_t size = 0;

    if (impl)
        size = impl->file_size;

    return size;
}

MPP_RET reader_read(FileReader reader, FileBufSlot **buf) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;
    FileBufSlot *slot = NULL;

    if (NULL == impl || NULL == impl->slots) {
        // // mpp_log_f("invalid reader %p\n", reader);
        return MPP_NOK;
    }

    if (impl->slot_rd_idx >= impl->slot_max) {
        // // mpp_log_f("invalid read index % max %d\n", impl->slot_rd_idx, impl->slot_max);
        return MPP_NOK;
    }

    do {
        slot = impl->slots[impl->slot_rd_idx];
        if (slot == NULL || (impl->slot_rd_idx > impl->slot_cnt))
            msleep(1);
    } while (slot == NULL);

    // mpp_assert(slot);

    *buf = slot;
    impl->slot_rd_idx++;

    return MPP_OK;
}

MPP_RET reader_index_read(FileReader reader, RK_S32 index, FileBufSlot **buf) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;
    FileBufSlot *slot = NULL;

    if (NULL == impl || NULL == impl->slots) {
        // // mpp_log_f("invalid reader %p\n", reader);
        return MPP_NOK;
    }

    if (index >= (RK_S32) impl->slot_max) {
        // // mpp_log_f("invalid read index % max %d\n", index, impl->slot_max);
        return MPP_NOK;
    }

    do {
        slot = impl->slots[index];
        if (slot == NULL)
            msleep(1);
    } while (slot == NULL);

    // mpp_assert(slot);

    *buf = slot;

    return MPP_OK;
}

void reader_rewind(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;

    impl->slot_rd_idx = 0;
}

void reader_init(FileReader *reader, char *file_in, MppCodingType type, size_t buf_size) {
    FILE *fp_input = NULL;
    FileReaderImpl *impl = NULL;
    if (reader == NULL) {
        return;
    }
    *reader = NULL;
    // step:1
    fp_input = fopen(file_in, "rb");
    if (fp_input == NULL) {
        printf("reader_init: failed to open input file %s\n", file_in);
        return;
    }
    // step 2:
    impl = (FileReaderImpl *) calloc(1, sizeof(FileReaderImpl));
    if (impl == NULL) {
        printf("reader_init: failed to alloc reader\n");
        fclose(fp_input);
        return;
    }
    // step 3: 文件大小，read_normal_file 用它判断是否读到文件末尾
    impl->fp_input = fp_input;
    fseek(fp_input, 0L, SEEK_END);
    impl->file_size = ftell(fp_input);
    fseek(fp_input, 0L, SEEK_SET);

    // step 4: 读取参数，
    if (buf_size < SZ_4K) {
        buf_size = SZ_4K;
    }
    //  每个slot装多少码流
    impl->buf_size = MPP_ALIGN(buf_size, SZ_4K);
    // 码流后面多留 256 字节余量
    impl->stuff_size = 256;
    // 按照固定长度读，分帧交给MPP
    impl->read_func = read_normal_file;
    // 先预留1024个slot ，不够时，add_new_slot
    impl->slot_max = 1024;
    impl->type = type;
    impl->file_type = FILE_NORMAL_TYPE;
    // step 5: slot 指针数组：每个元素使FileBufSlot*(指针)，不是FileBufSlot(结构体)
    impl->slots = (FileBufSlot **) calloc(impl->slot_max, sizeof(FileBufSlot *));
    if (impl->slots == NULL) {
        printf("reader_init: failed to alloc %u slots\n", impl->slot_max);
        fclose(fp_input);
        free(impl);
        return;
    }

    // step 6:
    reader_start(impl);

    *reader = impl;
}

void reader_deinit(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) (reader);
    RK_U32 i;

    // mpp_assert(impl);
    reader_stop(impl);

    if (impl->fp_input) {
        fclose(impl->fp_input);
        impl->fp_input = NULL;
    }

    for (i = 0; i < impl->slot_cnt; i++) {
        FileBufSlot *slot = impl->slots[i];
        if (!slot)
            continue;

        if (slot->buf) {
            mpp_buffer_put(slot->buf);
            slot->buf = NULL;
        }
        MPP_FREE(impl->slots[i]);
    }

    if (impl->group) {
        mpp_buffer_group_put(impl->group);
        impl->group = NULL;
    }

    MPP_FREE(impl->slots);
    MPP_FREE(impl);
}

static void *reader_worker(void *param) {
    FileReaderImpl *impl = (FileReaderImpl *) param;
    RK_U32 eos = 0;

    while (!impl->thd_stop && !eos) {
        FileBufSlot *slot = impl->read_func(impl);

        if (NULL == slot)
            break;

        add_new_slot(impl, slot);
        eos = slot->eos;
    }

    return NULL;
}

void reader_start(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;

    impl->thd_stop = 0;
    pthread_create(&impl->thd, NULL, reader_worker, impl);
}

void reader_sync(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;

    pthread_join(impl->thd, NULL);
    impl->thd_stop = 1;
}

void reader_stop(FileReader reader) {
    FileReaderImpl *impl = (FileReaderImpl *) reader;

    if (MPP_BOOL_CAS(&impl->thd_stop, 0, 1))
        pthread_join(impl->thd, NULL);
}

void show_dec_fps(RK_S64 total_time, RK_S64 total_count, RK_S64 last_time, RK_S64 last_count) {
    float avg_fps = (float) total_count * 1000000 / total_time;
    float ins_fps = (float) last_count * 1000000 / last_time;
}

RK_S32 mpi_dec_opt_i(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        strncpy(cmd->file_input, next, MAX_FILE_NAME_LENGTH - 1);
        cmd->have_input = 1;

        return 1;
    }

    // mpp_err("input file is invalid\n");
    return 0;
}

RK_S32 mpi_dec_opt_o(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        strncpy(cmd->file_output, next, MAX_FILE_NAME_LENGTH - 1);
        cmd->have_output = 1;
        return 1;
    }

    // mpp_log("output file is invalid\n");
    return 0;
}

RK_S32 mpi_dec_opt_w(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        cmd->width = atoi(next);
        return 1;
    }

    // mpp_err("invalid input width\n");
    return 0;
}

RK_S32 mpi_dec_opt_h(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        cmd->height = atoi(next);
        return 1;
    }

    // mpp_err("invalid input height\n");
    return 0;
}

RK_S32 mpi_dec_opt_t(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        MPP_RET ret;

        cmd->type = (MppCodingType) atoi(next);
        ret = mpp_check_support_format(MPP_CTX_DEC, cmd->type);
        if (!ret)
            return 1;
    }

    // mpp_err("invalid input coding type\n");
    return 0;
}

RK_S32 mpi_dec_opt_f(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        long number = 0;
        MppFrameFormat format = MPP_FMT_BUTT;
    }

    cmd->format = MPP_FMT_YUV420SP;
    return 0;
}

RK_S32 mpi_dec_opt_n(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        cmd->frame_num = atoi(next);

        if (cmd->frame_num < 0)
            // mpp_log("infinite loop decoding mode\n");

            return 1;
    }

    // mpp_err("invalid frame number\n");
    return 0;
}

RK_S32 mpi_dec_opt_s(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    cmd->nthreads = -1;
    if (next) {
        cmd->nthreads = atoi(next);
        if (cmd->nthreads >= 1)
            return 1;
    }

    // mpp_err("invalid nthreads %d\n", cmd->nthreads);
    cmd->nthreads = 1;
    return 0;
}

RK_S32 mpi_dec_opt_v(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        if (strstr(next, "q"))
            cmd->quiet = 1;
        if (strstr(next, "f"))
            cmd->trace_fps = 1;

        return 1;
    }

    return 0;
}

RK_S32 mpi_dec_opt_slt(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        size_t len = strnlen(next, MAX_FILE_NAME_LENGTH);
        if (len) {
            cmd->file_slt = calloc(len + 1, sizeof(char));
            strncpy(cmd->file_slt, next, len);

            return 1;
        }
    }

    // mpp_err("input slt verify file is invalid\n");
    return 0;
}

RK_S32 mpi_dec_opt_bufmode(void *ctx, const char *next) {
    MpiDecTestCmd *cmd = (MpiDecTestCmd *) ctx;

    if (next) {
        if (strstr(next, "hi")) {
            cmd->buf_mode = MPP_DEC_BUF_HALF_INT;
        } else if (strstr(next, "i")) {
            cmd->buf_mode = MPP_DEC_BUF_INTERNAL;
        } else if (strstr(next, "e")) {
            cmd->buf_mode = MPP_DEC_BUF_EXTERNAL;
        } else {
            cmd->buf_mode = MPP_DEC_BUF_HALF_INT;
        }

        return 1;
    }

    // mpp_err("invalid ext buf mode value\n");
    return 0;
}


RK_S32 mpi_dec_opt_help(void *ctx, const char *next) {
    (void) ctx;
    (void) next;
    /* return invalid option to print help */
    return -1;
}


void mpi_dec_test_cmd_options(MpiDecTestCmd *cmd) {
    if (cmd->quiet)
        return;

    printf("cmd parse result:\n");
    printf("input  file name: %s\n", cmd->file_input);
    printf("output file name: %s\n", cmd->file_output);
    printf("width      : %4d\n", cmd->width);
    printf("height     : %4d\n", cmd->height);
    printf("type       : %4d\n", cmd->type);
    printf("max frames : %4d\n", cmd->frame_num);
    if (cmd->file_slt)
        printf("verify     : %s\n", cmd->file_slt);
}

MPP_RET dec_buf_mgr_init(DecBufMgr *mgr) {
    DecBufMgrImpl *impl = NULL;
    MPP_RET ret = MPP_NOK;

    if (mgr) {
        impl = calloc(1, sizeof(DecBufMgrImpl));
        if (impl) {
            ret = MPP_OK;
        } else {
            // // mpp_err_f("failed to create decoder buffer manager\n");
        }

        *mgr = impl;
    }

    return ret;
}

void dec_buf_mgr_deinit(DecBufMgr mgr) {
    DecBufMgrImpl *impl = (DecBufMgrImpl *) mgr;

    if (NULL == impl)
        return;

    /* release buffer group for half internal and external mode */
    if (impl->group) {
        mpp_buffer_group_put(impl->group);
        impl->group = NULL;
    }

    /* release the buffers used in external mode */
    if (impl->buf_count && impl->bufs) {
        RK_U32 i;

        for (i = 0; i < impl->buf_count; i++) {
            if (impl->bufs[i]) {
                mpp_buffer_put(impl->bufs[i]);
                impl->bufs[i] = NULL;
            }
        }

        MPP_FREE(impl->bufs);
    }

    MPP_FREE(impl);
}

MppBufferGroup dec_buf_mgr_setup(DecBufMgr mgr, RK_U32 size, RK_U32 count, MppDecBufMode mode) {
    DecBufMgrImpl *impl = (DecBufMgrImpl *) mgr;
    MPP_RET ret = MPP_NOK;

    if (!impl)
        return NULL;

    /* cleanup old buffers if previous buffer group exists */
    if (impl->group) {
        if (mode != impl->buf_mode) {
            /* switch to different buffer mode just release old buffer group */
            mpp_buffer_group_put(impl->group);
            impl->group = NULL;
        } else {
            /* otherwise just cleanup old buffers */
            mpp_buffer_group_clear(impl->group);
        }

        /* if there are external mode old buffers do cleanup */
        if (impl->bufs) {
            RK_U32 i;

            for (i = 0; i < impl->buf_count; i++) {
                if (impl->bufs[i]) {
                    mpp_buffer_put(impl->bufs[i]);
                    impl->bufs[i] = NULL;
                }
            }

            MPP_FREE(impl->bufs);
        }
    }

    switch (mode) {
        case MPP_DEC_BUF_HALF_INT: {
            /* reuse previous half internal buffer group and just reconfig limit */
            if (NULL == impl->group) {
                ret = mpp_buffer_group_get_internal(&impl->group, MPP_BUFFER_TYPE_ION);
                if (ret) {
                    // // mpp_err_f("get mpp internal buffer group failed ret %d\n", ret);
                    break;
                }
            }
            /* Use limit config to limit buffer count and buffer size */
            ret = mpp_buffer_group_limit_config(impl->group, size, count);
            if (ret) {
                // // mpp_err_f("limit buffer group failed ret %d\n", ret);
            }
        } break;
        case MPP_DEC_BUF_INTERNAL: {
            /* do nothing juse keep buffer group empty */
            // mpp_assert(NULL == impl->group);
            ret = MPP_OK;
        } break;
        case MPP_DEC_BUF_EXTERNAL: {
            RK_U32 i;
            MppBufferInfo commit;

            impl->bufs = calloc(count, sizeof(MppBuffer));
            if (!impl->bufs) {
                // // mpp_err_f("create %d external buffer record failed\n", count);
                break;
            }

            /* reuse previous external buffer group */
            if (NULL == impl->group) {
                ret = mpp_buffer_group_get_external(&impl->group, MPP_BUFFER_TYPE_ION);
                if (ret) {
                    // // mpp_err_f("get mpp external buffer group failed ret %d\n", ret);
                    break;
                }
            }

            /*
             * NOTE: Use default misc allocater here as external allocator for demo.
             * But in practical case the external buffer could be GraphicBuffer or gst dmabuf.
             * The misc allocator will cause the print at the end like:
             * ~MppBufferService cleaning misc group
             */
            commit.type = MPP_BUFFER_TYPE_ION;
            commit.size = size;

            for (i = 0; i < count; i++) {
                ret = mpp_buffer_get(NULL, &impl->bufs[i], size);
                if (ret || NULL == impl->bufs[i]) {
                    // // mpp_err_f("get misc buffer failed ret %d\n", ret);
                    break;
                }

                commit.index = i;
                commit.ptr = mpp_buffer_get_ptr(impl->bufs[i]);
                commit.fd = mpp_buffer_get_fd(impl->bufs[i]);

                ret = mpp_buffer_commit(impl->group, &commit);
                if (ret) {
                    // // mpp_err_f("external buffer commit failed ret %d\n", ret);
                    break;
                }
            }
        } break;
        default: {
            // // mpp_err_f("unsupport buffer mode %d\n", mode);
        } break;
    }

    if (ret) {
        dec_buf_mgr_deinit(impl);
        impl = NULL;
    } else {
        impl->buf_count = count;
        impl->buf_size = size;
        impl->buf_mode = mode;
    }

    return impl ? impl->group : NULL;
}

void dump_mpp_frame_to_file(MppFrame frame, FILE *fp) {
    RK_U32 width = 0;
    RK_U32 height = 0;
    RK_U32 h_stride = 0;
    RK_U32 v_stride = 0;
    MppFrameFormat fmt = MPP_FMT_YUV420SP;
    MppBuffer buffer = NULL;
    RK_U8 *base = NULL;

    if (NULL == fp || NULL == frame)
        return;

    width = mpp_frame_get_width(frame);
    height = mpp_frame_get_height(frame);
    h_stride = mpp_frame_get_hor_stride(frame);
    v_stride = mpp_frame_get_ver_stride(frame);
    fmt = mpp_frame_get_fmt(frame);
    buffer = mpp_frame_get_buffer(frame);

    if (NULL == buffer)
        return;

    base = (RK_U8 *) mpp_buffer_get_ptr(buffer);

    if (MPP_FRAME_FMT_IS_RGB(fmt) && MPP_FRAME_FMT_IS_LE(fmt)) {
        fmt &= MPP_FRAME_FMT_MASK;
    }
    switch (fmt & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV422SP: {
            /* YUV422SP -> YUV422P for better display */
            RK_U32 i, j;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;
            RK_U8 *tmp = (RK_U8 *) malloc(h_stride * height * 2 * sizeof(RK_U8));
            RK_U8 *tmp_u = tmp;
            RK_U8 *tmp_v = tmp + width * height / 2;

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width, fp);

            for (i = 0; i < height; i++, base_c += h_stride) {
                for (j = 0; j < width / 2; j++) {
                    tmp_u[j] = base_c[2 * j + 0];
                    tmp_v[j] = base_c[2 * j + 1];
                }
                tmp_u += width / 2;
                tmp_v += width / 2;
            }

            fwrite(tmp, 1, width * height, fp);
            mpp_free(tmp);
        } break;
        case MPP_FMT_YUV420SP_VU:
        case MPP_FMT_YUV420SP: {
            RK_U32 i;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;

            for (i = 0; i < height; i++, base_y += h_stride) {
                fwrite(base_y, 1, width, fp);
            }
            for (i = 0; i < height / 2; i++, base_c += h_stride) {
                fwrite(base_c, 1, width, fp);
            }
        } break;
        case MPP_FMT_YUV420P: {
            RK_U32 i;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;

            for (i = 0; i < height; i++, base_y += h_stride) {
                fwrite(base_y, 1, width, fp);
            }
            for (i = 0; i < height / 2; i++, base_c += h_stride / 2) {
                fwrite(base_c, 1, width / 2, fp);
            }
            for (i = 0; i < height / 2; i++, base_c += h_stride / 2) {
                fwrite(base_c, 1, width / 2, fp);
            }
        } break;
        case MPP_FMT_YUV420SP_10BIT: {
            RK_U32 i, k;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;
            // malloc(width* sizeof(RK_U16));
            RK_U8 *tmp_line = (RK_U8 *) malloc(width * sizeof(RK_U16));

            if (!tmp_line) {
                // mpp_log("tmp_line malloc fail");
                return;
            }

            for (i = 0; i < height; i++, base_y += h_stride) {
                for (k = 0; k < MPP_ALIGN(width, 8) / 8; k++)
                    rearrange_pix(tmp_line, base_y, k);
                fwrite(tmp_line, width * sizeof(RK_U16), 1, fp);
            }

            for (i = 0; i < height / 2; i++, base_c += h_stride) {
                for (k = 0; k < MPP_ALIGN(width, 8) / 8; k++)
                    rearrange_pix(tmp_line, base_c, k);
                fwrite(tmp_line, width * sizeof(RK_U16), 1, fp);
            }

            MPP_FREE(tmp_line);
        } break;
        case MPP_FMT_YUV444SP: {
            /* YUV444SP -> YUV444P for better display */
            RK_U32 i, j;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;
            RK_U8 *tmp = (RK_U8 *) malloc(h_stride * height * 2 * sizeof(RK_U8));
            RK_U8 *tmp_u = tmp;
            RK_U8 *tmp_v = tmp + width * height;

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width, fp);

            for (i = 0; i < height; i++, base_c += h_stride * 2) {
                for (j = 0; j < width; j++) {
                    tmp_u[j] = base_c[2 * j + 0];
                    tmp_v[j] = base_c[2 * j + 1];
                }
                tmp_u += width;
                tmp_v += width;
            }

            fwrite(tmp, 1, width * height * 2, fp);
            mpp_free(tmp);
        } break;
        case MPP_FMT_YUV400: {
            RK_U32 i;
            RK_U8 *base_y = base;
            RK_U8 *tmp = (RK_U8 *) malloc(h_stride * height * sizeof(RK_U8));

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width, fp);

            mpp_free(tmp);
        } break;
        case MPP_FMT_ARGB8888:
        case MPP_FMT_ABGR8888:
        case MPP_FMT_BGRA8888:
        case MPP_FMT_RGBA8888: {
            RK_U32 i;
            RK_U8 *base_y = base;
            RK_U8 *tmp = (RK_U8 *) malloc(h_stride * height * 4 * sizeof(RK_U8));

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width * 4, fp);

            mpp_free(tmp);
        } break;
        case MPP_FMT_YUV422_YUYV:
        case MPP_FMT_YUV422_YVYU:
        case MPP_FMT_YUV422_UYVY:
        case MPP_FMT_YUV422_VYUY:
        case MPP_FMT_RGB565:
        case MPP_FMT_BGR565:
        case MPP_FMT_RGB555:
        case MPP_FMT_BGR555:
        case MPP_FMT_RGB444:
        case MPP_FMT_BGR444: {
            RK_U32 i;
            RK_U8 *base_y = base;
            // RK_U8 *tmp = mpp_malloc(RK_U8, width * height * 2);
            RK_U8 *tmp = (RK_U8 *) malloc(width * height * 2 * sizeof(RK_U8));

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width * 2, fp);

            mpp_free(tmp);
        } break;
        case MPP_FMT_RGB888: {
            RK_U32 i;
            RK_U8 *base_y = base;
            // RK_U8 *tmp = mpp_malloc(RK_U8, width * height * 3);
            RK_U8 *tmp = (RK_U8 *) malloc(width * height * 3 * sizeof(RK_U8));

            for (i = 0; i < height; i++, base_y += h_stride)
                fwrite(base_y, 1, width * 3, fp);

            mpp_free(tmp);
        } break;
        default: {
            printf("not supported format %d\n", fmt);
        } break;
    }
}


void mpp_osal_free(const char *caller, void *ptr) {
    free(ptr);
    ptr = NULL;
}
