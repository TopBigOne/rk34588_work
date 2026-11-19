/*
 * 本文件拆自 FFmpeg 6.0 fftools/ffplay.c（Copyright (c) 2003 Fabrice Bellard），
 * 遵循 GNU LGPL 2.1 或更高版本。函数体保持原样，仅调整 static 和 #include。
 *
 * MppPlayer 改动（都标了“MppPlayer”）：d->mppDecoder 不为 NULL 时，视频的
 * avcodec_receive_frame / avcodec_send_packet / avcodec_flush_buffers 换成 MPP 的对应函数
 */
#include "pipeline/decoder.h"
#include "common/player_options.h"

int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
    memset(d, 0, sizeof(Decoder));
    d->pkt = av_packet_alloc();
    if (!d->pkt)
        return AVERROR(ENOMEM);
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;
    d->pkt_serial = -1;
    return 0;
}

int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        if (d->queue->serial == d->pkt_serial) {
            do {
                if (d->queue->abort_request)
                    return -1;

                switch (d->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        if (d->mppDecoder) {
                            // MppPlayer：MPP 硬解取帧
                            ret = mppVideoDecoderReceiveFrame(d->mppDecoder, frame);
                            if (ret == AVERROR(EAGAIN) && mppVideoDecoderIsWaitingForOutput(d->mppDecoder)
                                && d->queue->serial == d->pkt_serial) {
                                // MPP 输入队列满了（或者在等结束前剩下的帧），再送包也没用：睡 1 ms 再取。
                                // ret 改成一个不是 EAGAIN 的值，do-while 的条件成立，会再转一圈，
                                // 开头还会检查 abort_request，退出时不会卡在这里。
                                // （不能写 continue：do-while 里 continue 会跳到条件判断，ret 是 EAGAIN 就直接出循环了）
                                // seek 了（serial 变了）就不等了，照常返回 EAGAIN，去取新包，取到新 serial 的包时会 flush
                                av_usleep(1000);
                                ret = AVERROR(EBUSY);
                            }
                        } else
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0) {
                            AVRational tb = (AVRational){1, frame->sample_rate};
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                d->next_pts = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;
                }
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    if (d->mppDecoder)
                        mppVideoDecoderFlush(d->mppDecoder); // MppPlayer：MPP 没有 open 过的 avctx 不能 flush
                    else
                    avcodec_flush_buffers(d->avctx);
                    return 0;
                }
                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (d->queue->nb_packets == 0)
                SDL_CondSignal(d->empty_queue_cond);
            if (d->packet_pending) {
                d->packet_pending = 0;
            } else {
                int old_serial = d->pkt_serial;
                if (packet_queue_get(d->queue, d->pkt, 1, &d->pkt_serial) < 0)
                    return -1;
                if (old_serial != d->pkt_serial) {
                    if (d->mppDecoder)
                        mppVideoDecoderFlush(d->mppDecoder); // MppPlayer：seek 后清空 MPP
                    else
                    avcodec_flush_buffers(d->avctx);
                    d->finished = 0;
                    d->next_pts = d->start_pts;
                    d->next_pts_tb = d->start_pts_tb;
                }
            }
            if (d->queue->serial == d->pkt_serial)
                break;
            av_packet_unref(d->pkt);
        } while (1);

        if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, d->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !d->pkt->data) {
                    d->packet_pending = 1;
                }
                ret = got_frame ? 0 : (d->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(d->pkt);
        } else if (d->mppDecoder) {
            // MppPlayer：MPP 硬解送包。EAGAIN = 上一个包还在等 MPP 输入队列，下一轮重送这个包
            int sendRet = mppVideoDecoderSendPacket(d->mppDecoder, d->pkt);
            if (sendRet == AVERROR(EAGAIN)) {
                d->packet_pending = 1;
            } else {
                if (sendRet < 0 && sendRet != AVERROR_EOF)
                    av_log(NULL, AV_LOG_ERROR, "MPP 送包失败：%s\n", av_err2str(sendRet));
                av_packet_unref(d->pkt);
            }
        } else {
            if (avcodec_send_packet(d->avctx, d->pkt) == AVERROR(EAGAIN)) {
                av_log(d->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                d->packet_pending = 1;
            } else {
                av_packet_unref(d->pkt);
            }
        }
    }
}

void decoder_destroy(Decoder *d) {
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->avctx);
    mppVideoDecoderRelease(&d->mppDecoder); // MppPlayer：还有帧没显示完时，真正销毁推迟到最后一帧释放

}

void decoder_abort(Decoder *d, FrameQueue *fq)
{
    packet_queue_abort(d->queue);
    frame_queue_signal(fq);
    SDL_WaitThread(d->decoder_tid, NULL);
    d->decoder_tid = NULL;
    packet_queue_flush(d->queue);
}

int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void* arg)
{
    packet_queue_start(d->queue);
    d->decoder_tid = SDL_CreateThread(fn, thread_name, arg);
    if (!d->decoder_tid) {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}
