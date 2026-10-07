/*
 * Copyright 2015 Rockchip Electronics Co. LTD
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

#include <cstdio>

#define MODULE_TAG "MPI_DEC_LIGHT"
#include <pthread.h>
#include <rockchip/rk_mpi.h>
#include <string.h>
#include <unistd.h>

#include "mpi_dec_utils.h" // 头文件自己带了 extern "C"，这里直接 include


#define MAX_FILE_NAME_LENGTH 256
#define msleep(x) usleep((x) * 1000)

typedef struct data_crc_t {
    RK_U32 len;
    RK_U32 sum_cnt;
    RK_ULONG *sum;
    RK_U32 vor; // value of the xor
} DataCrc;

typedef struct frame_crc_t {
    DataCrc luma;
    DataCrc chroma;
} FrmCrc;

/*
 * MpiDecLoopData - 解码循环的运行时数据
 *
 * 由 dec_decode() 在栈上创建并填充，传给 thread_decode() 解码线程使用。
 * 包含解码所需的全部上下文：MPP 实例、输入输出对象、统计信息等。
 *
 * 生命周期：dec_decode() 入口创建 → thread_decode() 线程中使用 → dec_decode() 出口销毁
 */
typedef struct {
    /* ---- MPP 核心对象 ---- */
    MpiDecTestCmd *cmd; /* 命令行参数（输入文件、编码类型、帧数等），只读 */
    MppCtx ctx; /* MPP 上下文（解码器实例），由 mpp_create 创建 */
    MppApi *mpi; /* MPI 函数指针集合（decode_put_packet 等），由 mpp_create 返回 */
    RK_U32 quiet; /* 静默模式标志：1=减少日志输出 */

    /* ---- 循环控制 ---- */
    RK_U32 loop_end; /* 解码结束标志：置 1 后解码循环退出。
                      * 由以下情况触发：文件读完且不循环、达到指定帧数、用户按 Enter */

    /* ---- 输入输出资源 ---- */
    DecBufMgr buf_mgr; /* buffer 管理器：管理帧 BufferGroup 的创建和销毁 */
    MppBufferGroup frm_grp; /* 帧 BufferGroup：解码输出帧的 DMA buffer 池。
                             * simple 模式在 info_change 时创建，advanced 模式在初始化时预分配 */
    MppPacket packet; /* 码流数据包：simple 模式复用同一个 packet，每次填入新数据 */
    MppFrame frame; /* 输出帧：仅 advanced（JPEG）模式使用，预分配了 buffer */

    /* ---- 输出文件 ---- */
    FILE *fp_output; /* YUV 输出文件指针（-o 参数指定），NULL 表示不保存 */

    /* ---- 帧计数 ---- */
    RK_S32 frame_count; /* 已解码的帧数（运行时递增） */
    RK_S32 frame_num; /* 目标帧数（来自 cmd->frame_num）：
                       * -1=无限循环, 0=解到 EOS, >0=解指定帧数后停止 */

    /* ---- 性能统计 ---- */
    RK_S64 first_pkt; /* 第一个 packet 送入解码器的时间戳（微秒） */
    RK_S64 first_frm; /* 第一帧解码输出的时间戳（微秒） */
    size_t max_usage; /* 帧 buffer 内存峰值使用量（字节） */
    float frame_rate; /* 平均解码帧率（fps），解码完成后计算 */
    RK_S64 elapsed_time; /* 总解码耗时（微秒） */
    RK_S64 delay; /* 首帧延迟 = first_frm - first_pkt（微秒），
                   * 衡量从送入第一包到输出第一帧的时间 */

    /* ---- CRC 校验 ---- */
    FILE *fp_verify; /* CRC 校验输出文件指针（--slt 参数），NULL 表示不校验 */
    FrmCrc checkcrc; /* 逐帧 CRC 计算上下文（luma + chroma），用于自动化正确性比对 */
} MpiDecLoopData;

/*
 * dec_simple - 简单模式解码函数（每次调用处理一个数据包）
 *
 * 整体流程：
 *   1. 从文件读取一包码流数据（如一帧 H.264 NALU）
 *   2. 把码流数据送进 MPP 解码器（decode_put_packet）
 *   3. 循环取出解码后的图像帧（decode_get_frame）
 *   4. 处理取到的帧：保存文件 / CRC 校验 / 统计帧率
 *
 * 调用方式：外层 while (!data->loop_end) 循环调用本函数，每次处理一个 packet，
 *           直到文件读完或达到指定帧数。
 *
 * 注意：这是"简单接口"（decode_put_packet / decode_get_frame），
 *       与 dec_advanced 使用的"高级 Task 接口"（poll/dequeue/enqueue）不同。
 *       简单接口更易上手，适合大多数场景。
 */
static int dec_simple(MpiDecLoopData *data) {
    RK_U32 pkt_done = 0; /* 标记当前 packet 是否已成功送入解码器 */
    RK_U32 pkt_eos = 0; /* 标记当前 packet 是否是码流的最后一包（End Of Stream） */
    MPP_RET ret = MPP_OK;
    MpiDecTestCmd *cmd = data->cmd;
    MppCtx ctx = data->ctx; /* MPP 上下文，代表一个解码器实例 */
    MppApi *mpi = data->mpi; /* MPI 函数指针集合，通过它调用 decode_put_packet 等 */
    MppPacket packet = data->packet; /* 复用同一个 MppPacket 对象，每次填入新数据 */
    FileBufSlot *slot = NULL;
    RK_U32 quiet = data->quiet;
    FrmCrc *checkcrc = &data->checkcrc;

    /* ================================================================
     * 第一步：从输入文件读取一包码流数据
     * reader 内部会按码流格式（H.264/H.265 等）做分包，
     * 每次返回一个 FileBufSlot，包含 data 指针、size 和 eos 标志。
     * ================================================================ */
    ret = reader_read(cmd->reader, &slot);
    pkt_eos = slot->eos; /* reader 读到文件末尾时会置 eos = 1 */

    /* 处理文件结束（EOS）情况 */
    if (pkt_eos) {
        if (data->frame_num < 0 || data->frame_num > data->frame_count) {
            /*
             * frame_num < 0 表示无限循环播放；
             * frame_num > frame_count 表示还没解够指定帧数，需要循环。
             * 此时回到文件开头重新读取，继续解码。
             */
            // printf(quiet, "%p loop again\n", ctx);
            reader_rewind(cmd->reader);
            pkt_eos = 0; /* 清掉 eos，让解码继续 */
        } else {
            /* 已经解够帧数，标记整个解码循环结束 */
            printf("found last packet: %d\n", quiet);
            printf(" start quite decode thread.");
            data->loop_end = 1;
        }
    }

    /* ================================================================
     * 第二步：把读到的码流数据填入 MppPacket
     *
     * MppPacket 是 MPP 对一维码流数据的封装：
     *   - data/size: 码流 buffer 的起始地址和总容量
     *   - pos/length: 当前未消费数据的起始位置和剩余长度
     *
     * 首次设置时 pos = data, length = size（整包数据都未消费）。
     * 如果 decode_put_packet 一次没消费完，pos 会前移、length 会减少，
     * 下次继续送剩余部分（本函数外层 do-while 会处理这种情况）。
     * ================================================================ */
    mpp_packet_set_data(packet, slot->data);
    mpp_packet_set_size(packet, slot->size);
    mpp_packet_set_pos(packet, slot->data);
    mpp_packet_set_length(packet, slot->size);
    /* 如果是最后一包，给 packet 打上 EOS 标志，通知解码器码流结束 */
    if (pkt_eos)
        mpp_packet_set_eos(packet);

    /* ================================================================
     * 第三步：送包 + 取帧 的主循环
     *
     * 外层 do-while：负责把 packet 送入解码器（可能需要重试）
     * 内层 do-while：负责取出所有已解码的帧
     *
     * 为什么要循环？
     *   - 解码器内部队列可能满了，decode_put_packet 会失败，需要等一下重试
     *   - 一个 packet 送进去后，解码器可能同时输出多帧（比如 B 帧参考链上的缓存帧）
     * ================================================================ */
    do {
        RK_U32 frm_eos = 0; /* 标记是否取到了带 EOS 标志的帧（解码器最终输出） */
        RK_S32 times = 30; /* decode_get_frame 超时重试次数（最多 30 次 × 1ms = 30ms） */

        /* ---- 送包：把码流 packet 送入解码器内部队列 ---- */
        if (!pkt_done) {
            puts("[decode-thread] put packet");
            ret = mpi->decode_put_packet(ctx, packet);
            if (MPP_OK == ret) {
                pkt_done = 1; /* 送成功，标记完成 */
            }
            /* 如果失败（队列满），pkt_done 仍为 0，外层循环会 sleep 后重试 */
        }

        /* ---- 取帧：循环取出所有可用的解码帧 ---- */
        do {
            RK_S32 get_frm = 0; /* 本轮是否成功取到了帧 */
            MppFrame frame = NULL;

        try_again:
            puts("[decode-thread] get frame");
            ret = mpi->decode_get_frame(ctx, &frame);
            if (MPP_ERR_TIMEOUT == ret) {
                /* 超时：解码器还在工作中，稍等重试 */
                if (times > 0) {
                    times--;
                    msleep(1);
                    goto try_again;
                }
            }
            if (ret) {
                break;
            }

            if (frame) {

                /*
                 * 取到帧后，首先检查是否是 info_change 事件。
                 *
                 * info_change 是 MPP 解码的关键机制：
                 * 解码器解析到 SPS/PPS 后，知道了视频的宽高、stride 等信息，
                 * 但还没有帧缓冲区来存放解码输出。它会发出一个特殊的 "info change" 帧，
                 * 告诉应用层："我需要这么大的 buffer，请分配好再通知我继续"。
                 *
                 * 应用层处理流程：
                 *   1. 读取 info_change 帧中的 width/height/stride/buf_size
                 *   2. 创建 BufferGroup 并分配足够的 buffer
                 *   3. 用 MPP_DEC_SET_EXT_BUF_GROUP 把 buffer 交给解码器
                 *   4. 用 MPP_DEC_SET_INFO_CHANGE_READY 通知解码器可以继续解码了
                 */
                if (mpp_frame_get_info_change(frame)) {
                    RK_U32 width = mpp_frame_get_width(frame);
                    RK_U32 height = mpp_frame_get_height(frame);
                    RK_U32 hor_stride = mpp_frame_get_hor_stride(frame); /* 水平步长（对齐后的宽） */
                    RK_U32 ver_stride = mpp_frame_get_ver_stride(frame); /* 垂直步长（对齐后的高） */
                    RK_U32 buf_size = mpp_frame_get_buf_size(frame); /* 解码器需要的单帧 buffer 大小 */
                    MppBufferGroup grp = NULL;


                    /*
                     * 创建 BufferGroup：
                     * buf_size = 单帧大小，24 = 最大 buffer 数量（参考帧 + 输出帧 + 余量），
                     * buf_mode 控制内存模式（内部分配 / 外部分配）
                     */
                    grp = dec_buf_mgr_setup(data->buf_mgr, buf_size, 24, cmd->buf_mode);
                    /* 把 BufferGroup 交给解码器，解码器后续从中取 buffer 存放解码输出 */
                    ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, grp);
                    if (ret) {
                        // printf("%p set buffer group failed ret %d\n", ctx, ret);
                        break;
                    }
                    data->frm_grp = grp;

                    /* 通知解码器：buffer 准备好了，可以继续解码 */
                    ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                    if (ret) {
                        // printf("%p info change ready failed ret %d\n", ctx, ret);
                        break;
                    }
                } else {
                    /*
                     * 正常的解码输出帧 —— 这才是我们要的图像数据。
                     * 帧数据在 frame 内部的 MppBuffer 中，格式为 YUV（NV12 等），
                     * 可以保存到文件、送显示、或做后处理。
                     */
                    char log_buf[256];
                    RK_S32 log_size = sizeof(log_buf) - 1;
                    RK_S32 log_len = 0;
                    RK_U32 err_info = mpp_frame_get_errinfo(frame); /* 非 0 表示该帧有错误 */
                    RK_U32 discard = mpp_frame_get_discard(frame); /* 非 0 表示该帧应被丢弃 */


                    /* 拼装日志信息 */
                    log_len +=
                            snprintf(log_buf + log_len, log_size - log_len, "decode get frame %d", data->frame_count);

                    /* 如果帧携带 meta 信息，提取 temporal_id（时域层级，SVC 分层编码用） */
                    if (mpp_frame_has_meta(frame)) {
                        MppMeta meta = mpp_frame_get_meta(frame);
                        RK_S32 temporal_id = 0;

                        mpp_meta_get_s32(meta, KEY_TEMPORAL_ID, &temporal_id);

                        log_len += snprintf(log_buf + log_len, log_size - log_len, " tid %d", temporal_id);
                    }

                    if (err_info || discard) {
                        log_len += snprintf(log_buf + log_len, log_size - log_len, " err %x discard %x", err_info,
                                            discard);
                    }
                    // printf(quiet, "%p %s\n", ctx, log_buf);

                    data->frame_count++;
                    /* 如果指定了输出文件且帧没有错误，将 YUV 数据写入文件 */
                    if (data->fp_output && !err_info) {
                        puts("[decode-thread] save YUV to the file....");
                        dump_mpp_frame_to_file(frame, data->fp_output);
                    }

                }
                frm_eos = mpp_frame_get_eos(frame); /* 检查该帧是否携带 EOS 标志 */
                mpp_frame_deinit(&frame); /* 释放帧（归还 buffer 给 BufferGroup） */
                get_frm = 1;
            }

            /* 统计帧 buffer 内存峰值使用量 */
            if (data->frm_grp) {
                size_t usage = mpp_buffer_group_usage(data->frm_grp);
                if (usage > data->max_usage)
                    data->max_usage = usage;
            }

            /*
             * EOS 处理：最后一个 packet 已送入，但还没取到带 EOS 标志的帧。
             * 这说明解码器内部还有缓存帧未输出（比如 B 帧重排序），需要继续取。
             */
            if (pkt_eos && pkt_done && !frm_eos) {
                msleep(1);
                continue;
            }

            if (frm_eos) {
                // printf(quiet, "%p found last packet\n", ctx);
                break;
            }

            /* 检查是否已解够指定帧数 */
            if ((data->frame_num > 0 && (data->frame_count >= data->frame_num)) || ((data->frame_num == 0) && frm_eos))
                break;

            /*
             * 如果本轮取到了帧（get_frm=1），继续尝试取下一帧（解码器可能还有输出）；
             * 如果没取到帧（get_frm=0），说明当前没有更多输出了，退出内层循环。
             */
            if (get_frm)
                continue;
            break;
        } while (1);

        /* 外层也检查帧数限制 */
        if ((data->frame_num > 0 && (data->frame_count >= data->frame_num)) || ((data->frame_num == 0) && frm_eos)) {
            data->loop_end = 1;
            break;
        }

        /* packet 已成功送入解码器，本次调用完成 */
        if (pkt_done)
            break;

        /*
         * 走到这里说明 decode_put_packet 失败了（解码器内部队列满）。
         * 等 1ms 让解码器消费掉一些数据后重试。
         * 1080p 硬件解码一帧约 2ms，所以 sleep 1ms 就够了。
         */
        msleep(1);
    } while (1);

    return ret;
}


/*
 * dec_advanced - 高级模式解码函数（使用 Task 接口，每次调用处理一帧）
 *
 * 与 dec_simple 的区别：
 *   - dec_simple 使用"简单接口"：decode_put_packet / decode_get_frame
 *     适合大多数场景，MPP 内部自动管理输入输出队列。
 *   - dec_advanced 使用"Task 接口"：poll / dequeue / enqueue
 *     应用自己管理 Task 的生命周期，可以精确控制输入输出 buffer 的绑定关系。
 *     主要用于 JPEG 解码（需要指定输出 buffer 和格式）。
 *
 * Task 接口的数据流：
 *   输入端口(INPUT)                        输出端口(OUTPUT)
 *   poll(INPUT) → 等待可用 task             poll(OUTPUT) → 等待解码完成
 *   dequeue(INPUT) → 取出空 task            dequeue(OUTPUT) → 取出已完成 task
 *   设置 task 的输入 packet 和输出 frame     从 task 中取出解码后的 frame
 *   enqueue(INPUT) → 送回给解码器            enqueue(OUTPUT) → 归还 task
 */
static int dec_advanced(MpiDecLoopData *data) {
    MPP_RET ret = MPP_OK;
    MpiDecTestCmd *cmd = data->cmd;
    MppCtx ctx = data->ctx;
    MppApi *mpi = data->mpi;
    MppPacket packet = NULL;
    MppFrame frame = data->frame; /* 复用预分配的 frame（含 buffer），JPEG 模式需要 */
    MppTask task = NULL;
    RK_U32 quiet = data->quiet;
    FileBufSlot *slot = NULL;
    FrmCrc *checkcrc = &data->checkcrc;

    /* 读取一包码流数据（index=0 表示下一包） */
    ret = reader_index_read(cmd->reader, 0, &slot);


    /* 用 slot 的 MppBuffer 创建 packet（零拷贝，共享同一块 buffer） */
    mpp_packet_init_with_buffer(&packet, slot->buf);

    if (slot->eos)
        mpp_packet_set_eos(packet);

    /* ================================================================
     * 输入端：把码流 packet 通过 Task 送入解码器
     * ================================================================ */

    /* poll 输入端口：阻塞等待，直到有空闲的 task 可用 */
    ret = mpi->poll(ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret) {
        // // // printf("%p mpp input poll failed\n", ctx);
        return ret;
    }

    /* 从输入队列取出一个空 task */
    ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
    if (ret) {
        // // // printf("%p mpp task input dequeue failed\n", ctx);
        return ret;
    }

    // // mpp_assert(task);

    /*
     * 给 task 绑定输入和输出：
     * - KEY_INPUT_PACKET: 要解码的码流数据
     * - KEY_OUTPUT_FRAME: 解码结果要写入的帧（预分配了 buffer）
     * 这是 Task 接口的核心：应用显式指定"用这个 packet 解码，结果放到这个 frame 里"
     */
    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, frame);

    /* 把填好的 task 送回输入队列，解码器开始工作 */
    ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
    if (ret) {
        // // // printf("%p mpp task input enqueue failed\n", ctx);
        return ret;
    }


    /* ================================================================
     * 输出端：等待解码完成，取出解码后的帧
     * ================================================================ */

    /* poll 输出端口：阻塞等待解码完成 */
    ret = mpi->poll(ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret) {
        // // // printf("%p mpp output poll failed\n", ctx);
        return ret;
    }

    /* 从输出队列取出已完成的 task */
    ret = mpi->dequeue(ctx, MPP_PORT_OUTPUT, &task);
    if (ret) {
        //  // // printf("%p mpp task output dequeue failed\n", ctx);
        return ret;
    }

    // // mpp_assert(task);

    if (task) {
        MppFrame frame_out = NULL;

        /* 从 task 中取出解码后的 frame（实际数据在之前绑定的 buffer 中） */
        mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frame_out);

        if (frame) {
            if (!data->first_frm)
                // data->first_frm = mpp_time();

                /* 将解码后的 YUV 数据写入输出文件 */
                if (data->fp_output)
                    dump_mpp_frame_to_file(frame, data->fp_output);


            data->frame_count++;

            if (mpp_frame_get_eos(frame_out)) {
                // // // printf(quiet, "%p found eos frame\n", ctx);
            }
        }

        /* 检查是否达到帧数限制或 EOS */
        if (data->frame_num > 0) {
            if (data->frame_count >= data->frame_num)
                data->loop_end = 1;
        } else if (data->frame_num == 0) {
            if (slot->eos)
                data->loop_end = 1;
        }

        /* 把已处理的 task 归还到输出队列，让解码器可以复用这个 task 槽位 */
        ret = mpi->enqueue(ctx, MPP_PORT_OUTPUT, task);
        // if (ret)
        // // // printf("%p mpp task output enqueue failed\n", ctx);
    }

    /* ================================================================
     * 回收输入端 task：释放输入 packet，归还 task 槽位
     *
     * 这一步是 Task 接口的"礼仪"——解码器用完 packet 后，
     * 应用需要从输入队列取回 task，释放 packet，再把空 task 归还。
     * 不做这一步会导致 task 泄漏，输入队列最终会满。
     * ================================================================ */
    if (0) {
        /* 简单做法：直接释放 packet（大多数情况下可以，但不够规范） */
        mpp_packet_deinit(&packet);
    } else {
        /* 规范做法：从输入队列取回 task，取出 packet 后释放，再归还空 task */
        ret = mpi->dequeue(ctx, MPP_PORT_INPUT, &task);
        if (ret) {
            // // // printf("%p mpp task input dequeue failed\n", ctx);
            return ret;
        }


        if (task) {
            MppPacket packet_out = NULL;

            mpp_task_meta_get_packet(task, KEY_INPUT_PACKET, &packet_out);

            /* 校验取回的 packet 和之前送入的是同一个 */
            if (!packet_out || packet_out != packet)
                //// // printf_f("mismatch packet %p -> %p\n", packet, packet_out);

                mpp_packet_deinit(&packet_out); /* 释放 packet */

            /* 把空 task 归还到输入队列，维持 task 池的平衡 */
            ret = mpi->enqueue(ctx, MPP_PORT_INPUT, task);
            // if (ret)
            //  // // printf("%p mpp task input enqueue failed\n", ctx);
        }
    }

    return ret;
}

/*
 * thread_decode - 解码工作线程入口
 *
 * 在独立线程中运行解码循环，根据 simple 标志选择：
 *   - simple 模式（非 JPEG）→ 循环调用 dec_simple
 *   - advanced 模式（JPEG）  → 循环调用 dec_advanced
 *
 * 线程结束后统计总耗时、帧率和首帧延迟。
 */
static void *thread_decode(void *arg) {
    puts("------------->decode thread start to run<-------------");

    auto *data = (MpiDecLoopData *) arg;
    MpiDecTestCmd *cmd = data->cmd;
    if (cmd->simple) {
        /*
         * 简单模式：非 JPEG 格式（H.264/H.265/VP9 等）
         * 循环调用 dec_simple，每次处理一个 packet，直到 loop_end 被置位
         */
        while (!data->loop_end) {
            dec_simple(data);
        }
    }
    puts("------------->decode thread done<---------------------");
    return NULL;
}

/*
 * dec_decode - 解码主函数（完整的解码器生命周期）
 *
 * 这是 mpi_dec_test 的核心函数，完整展示了 MPP 解码器的使用流程：
 *
 *   1. 打开输出文件
 *   2. 根据模式（simple/advanced）准备 packet 或 frame
 *   3. mpp_create  → 创建 MPP 实例
 *   4. mpp_init    → 初始化为解码器，指定编码格式
 *   5. control     → 配置解码参数（split_parse 等）
 *   6. 启动解码线程 → thread_decode
 *   7. 等待解码完成
 *   8. 释放所有资源（逆序释放，避免泄漏）
 */
int dec_decode(MpiDecTestCmd *cmd) {
    reader_init(&cmd->reader, cmd->file_input, cmd->type, SZ_4K);
    if (cmd->reader == nullptr) {
        printf("%s\n ", "   reader still is NULL");
        return MPP_NOK;
    }
    /* MPP 上下文和 API */
    MppCtx ctx = NULL;
    MppApi *mpi = NULL;

    /* 输入码流 / 输出帧 */
    MppPacket packet = NULL;
    MppFrame frame = NULL;

    /* 视频参数 */
    RK_U32 width = cmd->width;
    RK_U32 height = cmd->height;
    MppCodingType type = cmd->type; /* 编码格式：H.264/H.265/JPEG 等 */

    /* 解码配置 */
    MppDecCfg cfg = NULL;
    RK_U32 need_split = 1; /* 开启内部分帧器（见下方说明） */

    /* 资源 */
    MppBuffer frm_buf = NULL; /* advanced 模式预分配的帧 buffer */
    pthread_t thd;
    pthread_attr_t attr;
    MpiDecLoopData decLoopData;
    int ret = MPP_OK;

    // printf("mpi_dec_test start\n");
    memset(&decLoopData, 0, sizeof(decLoopData));
    pthread_attr_init(&attr);

    /*
     * 模式选择：非 JPEG 用 simple 模式，JPEG 用 advanced 模式。
     * JPEG 需要 advanced 模式是因为：
     * - JPEG 需要预先指定输出 buffer（不像 H.264 有 info_change 流程）
     * - JPEG 可能需要指定输出格式（YUV420/YUV422/RGB）
     */
    cmd->simple = (cmd->type != MPP_VIDEO_CodingMJPEG) ? (1) : (0);

    /* ================================================================
     * 第一步：打开输出文件和校验文件
     * ================================================================ */
    if (cmd->have_output) {
        decLoopData.fp_output = fopen(cmd->file_output, "w+b");
        if (nullptr == decLoopData.fp_output) {
            printf("    failed to open output file %s\n", cmd->file_output);
            goto MPP_TEST_OUT;
        }
    }

    if (cmd->file_slt) {
        decLoopData.fp_verify = fopen(cmd->file_slt, "wt");
    }

    /* ================================================================
     * 第二步：初始化 buffer 管理器和输入输出对象
     * ================================================================ */
    ret = dec_buf_mgr_init(&decLoopData.buf_mgr);
    if (ret) {
        // // printf("dec_buf_mgr_init failed\n");
        goto MPP_TEST_OUT;
    }

    if (cmd->simple) {
        /*
         * simple 模式：创建一个空的 MppPacket，后续每次 dec_simple 填入数据。
         * 帧 buffer 由解码器在 info_change 时自动管理，无需预分配。
         */
        ret = mpp_packet_init(&packet, nullptr, 0);
        if (ret) {
            printf("    mpp_packet_init failed\n");
            goto MPP_TEST_OUT;
        }
    }

    /* ================================================================
     * 第三步：创建并初始化 MPP 解码器
     *
     * mpp_create: 创建 MPP 实例，获得 ctx（上下文）和 mpi（API 函数集合）
     * mpp_init:   初始化为解码器（MPP_CTX_DEC），指定编码格式（H.264/H.265 等）
     *
     * 这两步完成后，解码器就可以工作了。
     * ================================================================ */
    ret = mpp_create(&ctx, &mpi);
    if (ret) {
        printf("mpp_create failed\n");
        goto MPP_TEST_OUT;
    }

    printf("    mpi_dec_light decoder test start w: %d h: %d type: %d\n", width, height, type);

    ret = mpp_init(ctx, MPP_CTX_DEC, type);
    if (ret) {
        printf("%p mpp_init failed\n", ctx);
        goto MPP_TEST_OUT;
    }

    /* ================================================================
     * 第四步：配置解码参数
     *
     * MPP 的配置流程："get → modify → set"
     *   1. MPP_DEC_GET_CFG: 获取当前默认配置
     *   2. mpp_dec_cfg_set_xxx: 修改需要的参数
     *   3. MPP_DEC_SET_CFG: 把修改后的配置写回
     * ================================================================ */
    mpp_dec_cfg_init(&cfg);

    ret = mpi->control(ctx, MPP_DEC_GET_CFG, cfg);
    if (ret) {
        printf("%p failed to get decoder cfg ret %d\n", ctx, ret);
        goto MPP_TEST_OUT;
    }

    /*
     * split_parse = 1：开启 MPP 内部分帧器。
     * 当输入数据不是按帧分好的（比如直接读文件的一大块数据），
     * MPP 内部会自动寻找 NALU 边界进行分帧。
     * 大多数场景都应该开启。
     */
    ret = mpp_dec_cfg_set_u32(cfg, "base:split_parse", need_split);
    if (ret) {
        // // printf("%p failed to set split_parse ret %d\n", ctx, ret);
        goto MPP_TEST_OUT;
    }

    ret = mpi->control(ctx, MPP_DEC_SET_CFG, cfg);
    if (ret) {
        // // printf("%p failed to set cfg %p ret %d\n", ctx, cfg, ret);
        goto MPP_TEST_OUT;
    }

    /* ================================================================
     * 第五步：填充线程数据结构，启动解码线程
     * ================================================================ */
    decLoopData.cmd = cmd;
    decLoopData.ctx = ctx;
    decLoopData.mpi = mpi;
    decLoopData.loop_end = 0;
    decLoopData.packet = packet;
    decLoopData.frame = frame;
    decLoopData.frame_count = 0;
    decLoopData.frame_num = cmd->frame_num; /* -1=无限循环, 0=解到 EOS, >0=指定帧数 */
    decLoopData.quiet = cmd->quiet;

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&thd, &attr, thread_decode, &decLoopData);
    if (ret) {
        printf("   failed to create thread for input ret %d\n", ret);
        goto MPP_TEST_OUT;
    }

    /*
     * 无限循环模式（frame_num < 0）：
     * 主线程等待用户按 Enter 键，然后设置 loop_end 通知解码线程退出。
     * 适合压力测试、长时间稳定性测试等场景。
     */
    if (cmd->frame_num < 0) {
        printf("*******************************************\n");
        printf("**** Press Enter to stop loop decoding ****\n");
        printf("*******************************************\n");

        getc(stdin);
        decLoopData.loop_end = 1;
    }

    /* 等待解码线程结束 */
    pthread_join(thd, NULL);

    cmd->max_usage = decLoopData.max_usage; /* 回传内存峰值使用量 */

    /* 重置解码器（清空内部缓存，为销毁做准备） */
    ret = mpi->reset(ctx);
    if (ret) {
        // // printf("%p mpi->reset failed\n", ctx);
        goto MPP_TEST_OUT;
    }

    /* ================================================================
     * 第六步：释放所有资源（逆序释放）
     *
     * 释放顺序很重要：
     *   packet/frame → ctx(mpp_destroy) → frm_buf → buf_mgr → 文件 → cfg
     * 先释放使用者，再释放被依赖的资源。
     * ================================================================ */
MPP_TEST_OUT:
    if (decLoopData.packet) {
        mpp_packet_deinit(&decLoopData.packet);
        decLoopData.packet = NULL;
    }

    if (frame) {
        mpp_frame_deinit(&frame);
        frame = NULL;
    }

    if (ctx) {
        mpp_destroy(ctx); /* 销毁 MPP 实例（释放内部所有编解码资源） */
        ctx = NULL;
    }
    if (cmd->reader) {
        reader_deinit(cmd->reader);
        cmd->reader = NULL;
    }

    if (!cmd->simple) {
        if (frm_buf) {
            mpp_buffer_put(frm_buf); /* 归还 buffer（引用计数 -1） */
            frm_buf = NULL;
        }
    }

    decLoopData.frm_grp = NULL;
    if (decLoopData.buf_mgr) {
        dec_buf_mgr_deinit(decLoopData.buf_mgr); /* 销毁 BufferGroup 管理器 */
        decLoopData.buf_mgr = NULL;
    }

    if (decLoopData.fp_output) {
        fclose(decLoopData.fp_output);
        decLoopData.fp_output = NULL;
    }

    if (decLoopData.fp_verify) {
        fclose(decLoopData.fp_verify);
        decLoopData.fp_verify = NULL;
    }

    if (cfg) {
        mpp_dec_cfg_deinit(cfg);
        cfg = NULL;
    }

    pthread_attr_destroy(&attr);

    return ret;
}

/*
 * main - 程序入口
 *
 * 使用方法示例：
 *   mpi_dec_test -i input.h264 -t 7 -o output.yuv -n 100
 *     -i: 输入码流文件
 *     -t: 编码类型（7=H.264, 16777220=H.265, 参见 MppCodingType）
 *     -o: 输出 YUV 文件（可选）
 *     -n: 解码帧数（-1=无限循环, 0=解到文件结束, >0=指定帧数）
 *     -w/-h: 视频宽高（JPEG 模式必须指定）
 */
int main(int argc, char **argv) {
    RK_S32 ret = 0;
    MpiDecTestCmd cmd_ctx;
    MpiDecTestCmd *cmd = &cmd_ctx;
    memset((void *) cmd, 0, sizeof(*cmd));
    cmd->format = MPP_FMT_BUTT; /* 输出格式默认无效值，表示不指定 */
    cmd->pkt_size = MPI_DEC_STREAM_SIZE; /* 每次读取的码流块大小 */

    cmd->simple = 1;
    cmd->frame_num = 60;
    cmd->width = 1080;
    cmd->height = 1920;
    cmd->type = MPP_VIDEO_CodingAVC;
    cmd->have_output = 1;

    snprintf(cmd->file_input, sizeof(cmd->file_input), "%s", "/userdata/av/aaa.264");
    snprintf(cmd->file_output, sizeof(cmd->file_input), "%s", "/userdata/av/bbb.yuv");
    /* 执行解码（完整的创建→解码→销毁流程） */
    ret = dec_decode(cmd);
    return ret;
}
