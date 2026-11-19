/**
 * @file
 * @brief MPP 视频硬解的实现，接口说明见 mpp_video_decoder.h
 */
#include "video/mpp_video_decoder.h"

#include <rockchip/rk_mpi.h> // mpp_create、MppApi（decode_put_packet / decode_get_frame / reset / control）

#include "libavcodec/bsf.h" // AVBSFContext：码流过滤器（这里用 h264_mp4toannexb / hevc_mp4toannexb）

/** 日志前缀，ffplay 自己的日志没有前缀，加上它好区分是哪个模块打的 */
#define MPP_LOG_PREFIX "[MppVideoDecoder] "

/**
 * 内存池最多几块 buffer。解码器自己要留参考帧（H.264 最多 16 帧），
 * ffplay 手里同时还拿着几帧：pictq 最多 3 帧 + 正在显示的 1 帧 + 滤镜里可能有 1 帧，24 块够用
 */
static const int kFrameBufferCount = 24;

struct MppVideoDecoder {
    MppCtx decoderCtx;              ///< MPP 解码器实例
    MppApi *decoderApi;             ///< MPP 函数表：decode_put_packet、decode_get_frame、reset、control
    MppDecCfg decoderCfg;           ///< 解码配置（只用来关掉 split_parse）
    MppBufferGroup frameGroup;      ///< 解码输出的内存池，第一次 info change 时创建
    AVBSFContext *annexbFilterCtx;  ///< mp4toannexb 码流过滤器：avcC 格式（4 字节长度开头）→ Annex B（起始码开头）

    AVPacket *annexbPacket;         ///< 过滤器输出的 Annex B 包，等着送进 MPP
    int hasPendingAnnexbPacket;     ///< 1 = annexbPacket 里有一个包，上次因为 MPP 输入队列满了没送进去
    int hasPendingEndOfStream;      ///< 1 = 调用方已经说了“码流结束”，结束包还没送进 MPP
    int hasSentEndOfStream;         ///< 1 = 结束包已经送进 MPP，在等剩下的帧
    int hasReachedEndOfStream;      ///< 1 = 已经取到带 EOS 标志的帧，后面没有帧了

    AVRational streamTimeBase;      ///< 视频流时间基，pts 的单位（只用来打日志）
    AVRational sampleAspectRatio;   ///< 像素宽高比，从流参数里抄来填到每一帧上
    enum AVColorRange colorRange;   ///< 颜色范围（TV 16～235 / PC 0～255），SDL 选 YUV 转 RGB 公式要用
    enum AVColorSpace colorSpace;   ///< 颜色空间（BT.601 / BT.709），同上
    enum AVColorPrimaries colorPrimaries; ///< 三原色定义，原样填到帧上
    enum AVColorTransferCharacteristic colorTransfer; ///< 传输特性（gamma），原样填到帧上
    int infoChangeCount;            ///< info change 发生了几次（正常文件是 1 次）

    AVBufferRef *selfRef;           ///< 调用方持有的那份引用；每个没释放的 AVFrame 也各持有一份
};

/** 每个交出去的 AVFrame 背后挂着的东西：av_frame_unref 引用计数到 0 时，用它把 MppFrame 还给 MPP */
typedef struct MppFrameHolder {
    MppFrame mppFrame;          ///< MPP 解出来的帧，释放它 = 把 buffer 还给内存池
    MppBuffer frameBuffer;      ///< mppFrame 里的 buffer（像素数据在这里）
    AVBufferRef *decoderRef;    ///< 解码器的一份引用：帧没释放之前，解码器和内存池不能销毁
} MppFrameHolder;

/**
 * @brief 把 FFmpeg 的编码格式换成 MPP 的
 * @param[in] codecId  FFmpeg 的编码格式
 * @return MPP 的编码格式；不支持时返回 MPP_VIDEO_CodingUnused
 * @note H.264 在 MPP 里叫 AVC，H.265 叫 HEVC，是同一个东西的两个名字
 */
static MppCodingType convertCodecIdToMppCodingType(enum AVCodecID codecId) {
    switch (codecId) {
        case AV_CODEC_ID_H264:
            return MPP_VIDEO_CodingAVC;
        case AV_CODEC_ID_HEVC:
            return MPP_VIDEO_CodingHEVC;
        default:
            return MPP_VIDEO_CodingUnused;
    }
}

int mppVideoDecoderIsSupported(enum AVCodecID codecId) {
    return convertCodecIdToMppCodingType(codecId) != MPP_VIDEO_CodingUnused;
}

/**
 * @brief 真正销毁解码器（最后一份引用释放时，由 av_buffer_unref 回调）
 * @param[in] opaque  av_buffer_create 时传的 opaque，这里没用
 * @param[in] data    av_buffer_create 时传的 data，就是 MppVideoDecoder*
 * @return 无
 * @note 顺序：先 mpp_destroy（解码器不再用内存池），再释放内存池。
 *       走到这里时所有 AVFrame 都已经释放，池子里没有被占用的 buffer
 */
static void destroyMppVideoDecoder(void *opaque, uint8_t *data) {
    (void)opaque;
    MppVideoDecoder *decoder = (MppVideoDecoder *)data;

    if (decoder->decoderCfg) {
        mpp_dec_cfg_deinit(decoder->decoderCfg);
    }
    if (decoder->decoderCtx) {
        mpp_destroy(decoder->decoderCtx);
    }
    if (decoder->frameGroup) {
        // mpp_buffer_group_put：释放整个组和组里的所有 buffer（对应 mpp_buffer_group_get_internal）
        mpp_buffer_group_put(decoder->frameGroup);
    }
    av_bsf_free(&decoder->annexbFilterCtx); // 传指针的地址：释放后顺便置 NULL；传进去的是 NULL 也没关系
    av_packet_free(&decoder->annexbPacket);
    av_free(decoder);
    av_log(NULL, AV_LOG_VERBOSE, MPP_LOG_PREFIX "已销毁\n");
}

/**
 * @brief 创建 mp4toannexb 码流过滤器
 * @param[in,out] decoder          解码器，结果写到 decoder->annexbFilterCtx
 * @param[in]     codecParameters  视频流参数，extradata 里有 avcC/hvcC 格式的 SPS/PPS
 * @return 0 = 成功；负数 = FFmpeg 错误码
 * @note 为什么要转：mp4 里每个 NAL 前面是 4 字节的长度，SPS/PPS 单独放在 extradata 里；
 *       MPP（和裸 .h264 文件一样）要的是 Annex B：每个 NAL 前面是起始码 00 00 00 01，SPS/PPS 放在关键帧前面。
 *       输入已经是 Annex B 时（比如 .ts 文件），过滤器原样放行
 */
static int createAnnexbFilter(MppVideoDecoder *decoder, const AVCodecParameters *codecParameters) {
    const char *filterName = codecParameters->codec_id == AV_CODEC_ID_H264 ? "h264_mp4toannexb" : "hevc_mp4toannexb";
    // av_bsf_get_by_name：按名字找过滤器；FFmpeg 编译时没打开它就返回 NULL
    const AVBitStreamFilter *annexbFilter = av_bsf_get_by_name(filterName);
    if (!annexbFilter) {
        av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "FFmpeg 里没有 %s\n", filterName);
        return AVERROR_BSF_NOT_FOUND;
    }
    int ret = av_bsf_alloc(annexbFilter, &decoder->annexbFilterCtx);
    if (ret < 0) {
        return ret;
    }
    // par_in：告诉过滤器输入流的参数，它从 extradata 里读出 SPS/PPS，转好后插到每个关键帧前面
    ret = avcodec_parameters_copy(decoder->annexbFilterCtx->par_in, codecParameters);
    if (ret < 0) {
        return ret;
    }
    decoder->annexbFilterCtx->time_base_in = decoder->streamTimeBase; // 输入包 pts 的单位，过滤器不改 pts
    return av_bsf_init(decoder->annexbFilterCtx);
}

/**
 * @brief 创建并初始化 MPP 解码器
 * @param[in,out] decoder     解码器，结果写到 decoderCtx / decoderApi / decoderCfg
 * @param[in]     codingType  MPP 的编码格式
 * @return 0 = 成功；AVERROR_EXTERNAL = MPP 返回了错误（已打印）
 * @note 第 1 步 创建；第 2 步 初始化成解码器；第 3 步 关掉内部分帧（split_parse = 0）
 */
static int createMppDecoder(MppVideoDecoder *decoder, MppCodingType codingType) {
    // 第 1 步 mpp_create 写回两个值：实例和函数表，所以传它们的地址
    MPP_RET mppRet = mpp_create(&decoder->decoderCtx, &decoder->decoderApi);
    if (mppRet != MPP_OK) {
        av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "mpp_create ret=%d\n", mppRet);
        return AVERROR_EXTERNAL;
    }
    // 第 2 步 初始化成“解码器”（MPP_CTX_DEC），H.264 / H.265 用的是同一套接口，只是 codingType 不同
    mppRet = mpp_init(decoder->decoderCtx, MPP_CTX_DEC, codingType);
    if (mppRet != MPP_OK) {
        av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "mpp_init ret=%d\n", mppRet);
        return AVERROR_EXTERNAL;
    }
    // 第 3 步 split_parse = 0：不让 MPP 自己找帧边界。mp4 拆出来的每个包本来就是完整的一帧、带一个 pts；
    // 打开内部分帧的话，MPP 会把包拼起来再切开，pts 和帧就可能对不上
    mppRet = mpp_dec_cfg_init(&decoder->decoderCfg);
    if (mppRet == MPP_OK) {
        // GET_CFG 先把当前配置读出来，改一项，再 SET_CFG 写回去
        mppRet = decoder->decoderApi->control(decoder->decoderCtx, MPP_DEC_GET_CFG, decoder->decoderCfg);
    }
    if (mppRet == MPP_OK) {
        mppRet = mpp_dec_cfg_set_u32(decoder->decoderCfg, "base:split_parse", 0);
    }
    if (mppRet == MPP_OK) {
        mppRet = decoder->decoderApi->control(decoder->decoderCtx, MPP_DEC_SET_CFG, decoder->decoderCfg);
    }
    if (mppRet != MPP_OK) {
        av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "设置 split_parse 失败 ret=%d\n", mppRet);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

MppVideoDecoder *mppVideoDecoderCreate(const AVCodecParameters *codecParameters, AVRational streamTimeBase) {
    const MppCodingType codingType = convertCodecIdToMppCodingType(codecParameters->codec_id);
    if (codingType == MPP_VIDEO_CodingUnused) {
        return NULL;
    }

    // av_mallocz：分配并清零，所有指针成员一开始都是 NULL，失败时 destroyMppVideoDecoder 能安全地逐个释放
    MppVideoDecoder *decoder = av_mallocz(sizeof(MppVideoDecoder));
    if (!decoder) {
        return NULL;
    }
    // 把解码器自己包成一个带引用计数的 AVBufferRef：引用计数到 0 时 FFmpeg 调 destroyMppVideoDecoder。
    // 这里的 data 不是像素，只是借 AVBufferRef 的引用计数用
    decoder->selfRef = av_buffer_create((uint8_t *)decoder, sizeof(MppVideoDecoder), destroyMppVideoDecoder, NULL, 0);
    if (!decoder->selfRef) {
        av_free(decoder);
        return NULL;
    }
    decoder->streamTimeBase = streamTimeBase;
    decoder->sampleAspectRatio = codecParameters->sample_aspect_ratio;
    decoder->colorRange = codecParameters->color_range;
    decoder->colorSpace = codecParameters->color_space;
    decoder->colorPrimaries = codecParameters->color_primaries;
    decoder->colorTransfer = codecParameters->color_trc;

    decoder->annexbPacket = av_packet_alloc();
    if (!decoder->annexbPacket || createAnnexbFilter(decoder, codecParameters) < 0 || createMppDecoder(decoder, codingType) < 0) {
        mppVideoDecoderRelease(&decoder);
        return NULL;
    }
    av_log(NULL, AV_LOG_INFO, MPP_LOG_PREFIX "使用 MPP 硬解 %s，%dx%d\n",
           avcodec_get_name(codecParameters->codec_id), codecParameters->width, codecParameters->height);
    return decoder;
}

/**
 * @brief 把手里待送的东西（过滤器输出的包、结束包）尽量送进 MPP
 * @param[in,out] decoder  解码器
 * @return 0 = 送完了，或者 MPP 输入队列满了先留着下次再送；负数 = 出错
 * @note decode_put_packet 返回 MPP_ERR_BUFFER_FULL 不是错误，只是 MPP 输入队列满了，要先取走几帧
 */
static int putPendingInputToMpp(MppVideoDecoder *decoder) {
    for (;;) {
        // 第 1 步 手里没有待送的包，就从过滤器再拿一个（一个输入包一般只出一个包）
        if (!decoder->hasPendingAnnexbPacket) {
            int ret = av_bsf_receive_packet(decoder->annexbFilterCtx, decoder->annexbPacket);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // 过滤器里没有了
            }
            if (ret < 0) {
                return ret;
            }
            decoder->hasPendingAnnexbPacket = 1;
        }

        // 第 2 步 包成 MppPacket：mpp_packet_init 不复制数据，只记下地址和长度；
        // decode_put_packet 内部会把数据复制走，所以送完就可以 deinit
        MppPacket mppPacket = NULL;
        if (mpp_packet_init(&mppPacket, decoder->annexbPacket->data, decoder->annexbPacket->size) != MPP_OK) {
            return AVERROR(ENOMEM);
        }
        // pts 跟着包走，MPP 不管单位，B 帧重排后原样带到对应的输出帧上（mpp_frame_get_pts 取回）。
        // 没有 pts 的包用 dts 顶上
        const int64_t packetPts = decoder->annexbPacket->pts != AV_NOPTS_VALUE ? decoder->annexbPacket->pts
                                                                                : decoder->annexbPacket->dts;
        mpp_packet_set_pts(mppPacket, packetPts);
        const MPP_RET putRet = decoder->decoderApi->decode_put_packet(decoder->decoderCtx, mppPacket);
        mpp_packet_deinit(&mppPacket);

        if (putRet == MPP_ERR_BUFFER_FULL) {
            return 0; // MPP 输入队列满了：包留在 annexbPacket 里，取走几帧以后再送
        }
        if (putRet != MPP_OK) {
            av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "decode_put_packet ret=%d\n", putRet);
            return AVERROR_EXTERNAL;
        }
        av_packet_unref(decoder->annexbPacket);
        decoder->hasPendingAnnexbPacket = 0;
    }

    // 第 3 步 过滤器里的包都送完了，再送结束包：数据 NULL、长度 0、打上 EOS 标志
    if (decoder->hasPendingEndOfStream) {
        MppPacket endOfStreamPacket = NULL;
        if (mpp_packet_init(&endOfStreamPacket, NULL, 0) != MPP_OK) {
            return AVERROR(ENOMEM);
        }
        mpp_packet_set_eos(endOfStreamPacket);
        const MPP_RET putRet = decoder->decoderApi->decode_put_packet(decoder->decoderCtx, endOfStreamPacket);
        mpp_packet_deinit(&endOfStreamPacket);
        if (putRet == MPP_ERR_BUFFER_FULL) {
            return 0;
        }
        if (putRet != MPP_OK) {
            av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "decode_put_packet(EOS) ret=%d\n", putRet);
            return AVERROR_EXTERNAL;
        }
        decoder->hasPendingEndOfStream = 0;
        decoder->hasSentEndOfStream = 1;
    }
    return 0;
}

int mppVideoDecoderSendPacket(MppVideoDecoder *decoder, const AVPacket *packet) {
    if (decoder->hasPendingAnnexbPacket || decoder->hasPendingEndOfStream) {
        return AVERROR(EAGAIN); // 上一个还没送进 MPP，规则和 avcodec_send_packet 一样：先 receive
    }
    if (decoder->hasSentEndOfStream) {
        return AVERROR_EOF; // 结束以后不收新包，要先 flush
    }

    if (!packet || !packet->data) {
        // 空包 = 码流结束。先让过滤器吐出它手里剩下的（mp4toannexb 不会留包，这里只是按规矩来）
        av_bsf_send_packet(decoder->annexbFilterCtx, NULL);
        decoder->hasPendingEndOfStream = 1;
    } else {
        // av_bsf_send_packet 会“拿走”包里的数据引用（之后包被清空），而 packet 是 const，
        // 所以先 av_packet_ref 出一份新引用（只加引用计数，不复制数据）交给它
        AVPacket *packetRef = av_packet_alloc();
        if (!packetRef) {
            return AVERROR(ENOMEM);
        }
        int ret = av_packet_ref(packetRef, packet);
        if (ret >= 0) {
            ret = av_bsf_send_packet(decoder->annexbFilterCtx, packetRef);
        }
        av_packet_free(&packetRef);
        if (ret < 0) {
            return ret;
        }
    }
    return putPendingInputToMpp(decoder);
}

/**
 * @brief 处理 info change：按新的 buffer 大小准备内存池，告诉 MPP“准备好了”
 * @param[in,out] decoder          解码器
 * @param[in]     infoChangeFrame  带 info change 标志的帧（没有画面），从里面读宽高、stride、buffer 大小
 * @return 0 = 成功；AVERROR_EXTERNAL = MPP 返回了错误
 * @note 解码器解析完第一个 SPS 才知道一帧要多大，所以第一帧之前一定会来一次；分辨率变了会再来。
 *       内存池用 DRM 类型 + CACHABLE：显示时要用 CPU 读像素（SDL 上传纹理），
 *       不带 cache 的内存 CPU 读起来很慢；带 cache 就要在 CPU 读之前 sync（见 wrapMppFrameAsAvFrame）
 */
static int handleInfoChange(MppVideoDecoder *decoder, MppFrame infoChangeFrame) {
    // 第 1 步 读新参数
    const RK_U32 width = mpp_frame_get_width(infoChangeFrame);
    const RK_U32 height = mpp_frame_get_height(infoChangeFrame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(infoChangeFrame); // 每行实际占多少字节（≥ 宽，硬件要求对齐）
    const RK_U32 verStride = mpp_frame_get_ver_stride(infoChangeFrame); // Y 平面实际有多少行（≥ 高，比如 1080 → 1088）
    const size_t bufSizeBytes = mpp_frame_get_buf_size(infoChangeFrame); // 一帧要多大的 buffer
    decoder->infoChangeCount++;
    av_log(NULL, AV_LOG_INFO, MPP_LOG_PREFIX "info change：%ux%u，stride %ux%u，buf_size %zu\n", width, height,
           horStride, verStride, bufSizeBytes);

    // 第 2 步 准备内存池：第一次新建；以后再来就清空重用
    MPP_RET mppRet = MPP_OK;
    if (decoder->frameGroup) {
        mppRet = mpp_buffer_group_clear(decoder->frameGroup);
    } else {
        mppRet = mpp_buffer_group_get_internal(&decoder->frameGroup, MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE);
    }
    // limit_config：每块 bufSizeBytes 字节，最多 kFrameBufferCount 块；用光时解码器停下来等，不会无限申请
    if (mppRet == MPP_OK) {
        mppRet = mpp_buffer_group_limit_config(decoder->frameGroup, bufSizeBytes, kFrameBufferCount);
    }
    // 第 3 步 把内存池交给解码器（EXT_BUF_GROUP），再告诉它“准备好了”，它才会接着解
    if (mppRet == MPP_OK) {
        mppRet = decoder->decoderApi->control(decoder->decoderCtx, MPP_DEC_SET_EXT_BUF_GROUP, decoder->frameGroup);
    }
    if (mppRet == MPP_OK) {
        mppRet = decoder->decoderApi->control(decoder->decoderCtx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
    }
    if (mppRet != MPP_OK) {
        av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "处理 info change 失败 ret=%d\n", mppRet);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

/**
 * @brief AVFrame 的引用计数到 0 时，FFmpeg 回调它：把 MppFrame 还给 MPP
 * @param[in] opaque  MppFrameHolder*
 * @param[in] data    像素地址（这里不用）
 * @return 无
 * @note 可能在显示线程（主线程）里被调用，和解码线程同时用 MPP；MPP 的 buffer 引用计数自带锁，没问题。
 *       最后放掉解码器的引用：如果解码器已经被 Release，这里就是最后一份，会顺带销毁解码器
 */
static void releaseMppFrame(void *opaque, uint8_t *data) {
    (void)data;
    MppFrameHolder *holder = (MppFrameHolder *)opaque;
    mpp_buffer_sync_ro_end(holder->frameBuffer); // 和 sync_ro_begin 配对：CPU 读完了
    mpp_frame_deinit(&holder->mppFrame);         // buffer 引用计数减 1，回到内存池给解码器再用
    av_buffer_unref(&holder->decoderRef);
    av_free(holder);
}

/**
 * @brief 把 MPP 解出来的 NV12 帧包成 AVFrame，不复制像素
 * @param[in]  decoder   解码器
 * @param[in]  mppFrame  有画面的 MppFrame，所有权交给本函数（成功时挂到 AVFrame 上，失败时释放）
 * @param[out] frame     输出帧（调用方传进来的是空帧）
 * @return 0 = 成功；负数 = 失败（mppFrame 已释放）
 * @note NV12 的内存布局：Y 平面 horStride × verStride 字节，紧跟着 UV 平面（U、V 交替），一行也是 horStride 字节。
 *       所以 data[1] = data[0] + horStride × verStride，而不是 + horStride × height
 */
static int wrapMppFrameAsAvFrame(MppVideoDecoder *decoder, MppFrame mppFrame, AVFrame *frame) {
    MppBuffer frameBuffer = mpp_frame_get_buffer(mppFrame);
    const RK_U32 horStride = mpp_frame_get_hor_stride(mppFrame);
    const RK_U32 verStride = mpp_frame_get_ver_stride(mppFrame);

    MppFrameHolder *holder = av_mallocz(sizeof(MppFrameHolder));
    if (!holder) {
        mpp_frame_deinit(&mppFrame);
        return AVERROR(ENOMEM);
    }
    holder->mppFrame = mppFrame;
    holder->frameBuffer = frameBuffer;
    holder->decoderRef = av_buffer_ref(decoder->selfRef); // 帧活着，解码器就不能销毁
    if (!holder->decoderRef) {
        mpp_frame_deinit(&holder->mppFrame);
        av_free(holder);
        return AVERROR(ENOMEM);
    }

    // CPU 读之前同步 cache：硬件解码器是直接写内存的，CPU cache 里可能还是这块内存的旧数据，
    // sync_ro_begin 让 CPU 丢掉旧的 cache 行，接下来读到的才是新画面
    mpp_buffer_sync_ro_begin(frameBuffer);
    // mpp_buffer_get_ptr：buffer 在本进程里的虚拟地址（MPP 分配时已经 mmap 好了）
    uint8_t *pixelAddress = (uint8_t *)mpp_buffer_get_ptr(frameBuffer);

    // av_buffer_create：把一块别人管理的内存包成 AVBufferRef，不复制。
    // 引用计数到 0 时调用 releaseMppFrame(holder, ...)；READONLY：告诉 FFmpeg 不要往里写
    frame->buf[0] = av_buffer_create(pixelAddress, mpp_buffer_get_size(frameBuffer), releaseMppFrame, holder,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        releaseMppFrame(holder, NULL);
        return AVERROR(ENOMEM);
    }

    frame->format = AV_PIX_FMT_NV12;
    frame->width = (int)mpp_frame_get_width(mppFrame);
    frame->height = (int)mpp_frame_get_height(mppFrame);
    frame->data[0] = pixelAddress;                                   // Y 平面
    frame->data[1] = pixelAddress + (size_t)horStride * verStride;   // UV 平面
    frame->linesize[0] = (int)horStride;                             // Y 平面每行字节数
    frame->linesize[1] = (int)horStride;                             // UV 平面每行字节数（U、V 交替，所以和 Y 一样宽）

    // pts：送包时带进去的值，单位是视频流的 time_base。
    // ffplay 默认用 best_effort_timestamp 当 pts（decoder_reorder_pts = -1），两个都填上
    frame->pts = mpp_frame_get_pts(mppFrame);
    frame->best_effort_timestamp = frame->pts;
    frame->pkt_dts = AV_NOPTS_VALUE;

    frame->sample_aspect_ratio = decoder->sampleAspectRatio;
    frame->color_range = decoder->colorRange;
    frame->colorspace = decoder->colorSpace;
    frame->color_primaries = decoder->colorPrimaries;
    frame->color_trc = decoder->colorTransfer;
    return 0;
}

int mppVideoDecoderReceiveFrame(MppVideoDecoder *decoder, AVFrame *frame) {
    if (decoder->hasReachedEndOfStream) {
        return AVERROR_EOF;
    }
    // 第 1 步 上次因为 MPP 输入队列满了没送进去的包，先再试一次
    int ret = putPendingInputToMpp(decoder);
    if (ret < 0) {
        return ret;
    }

    for (;;) {
        // 第 2 步 取一帧。MPP 默认的输出超时是“不等待”：没有帧就马上返回，mppFrame 是 NULL
        MppFrame mppFrame = NULL;
        const MPP_RET getRet = decoder->decoderApi->decode_get_frame(decoder->decoderCtx, &mppFrame);
        if (getRet != MPP_OK) {
            av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "decode_get_frame ret=%d\n", getRet);
            return AVERROR_EXTERNAL;
        }
        if (!mppFrame) {
            return AVERROR(EAGAIN);
        }

        // 第 3 步 info change 帧没有画面，只是告诉我们“宽高定了，请准备内存池”
        if (mpp_frame_get_info_change(mppFrame)) {
            ret = handleInfoChange(decoder, mppFrame);
            mpp_frame_deinit(&mppFrame);
            if (ret < 0) {
                return ret;
            }
            continue;
        }

        // 第 4 步 EOS 标志可能挂在最后一个有画面的帧上，也可能单独一个没有 buffer 的空帧
        const int isEndOfStream = mpp_frame_get_eos(mppFrame) != 0;
        if (isEndOfStream) {
            decoder->hasReachedEndOfStream = 1;
        }
        if (!mpp_frame_get_buffer(mppFrame)) {
            mpp_frame_deinit(&mppFrame);
            if (isEndOfStream) {
                return AVERROR_EOF;
            }
            continue;
        }

        // 第 5 步 解坏的帧（errinfo：码流错误；discard：参考帧缺失，比如 seek 后第一个关键帧之前的帧）不显示
        if (mpp_frame_get_errinfo(mppFrame) || mpp_frame_get_discard(mppFrame)) {
            av_log(NULL, AV_LOG_DEBUG, MPP_LOG_PREFIX "丢掉一个出错的帧 pts=%" PRId64 "\n",
                   (int64_t)mpp_frame_get_pts(mppFrame)); // RK_S64 是 long long，PRId64 在 aarch64 上是 long，转一下才对得上
            mpp_frame_deinit(&mppFrame);
            if (isEndOfStream) {
                return AVERROR_EOF;
            }
            continue;
        }

        // 第 6 步 只认 NV12（8 bit 4:2:0）。10 bit 的片源 MPP 输出的是 NV15，SDL 不认识，先不支持
        const MppFrameFormat pixelFormat = mpp_frame_get_fmt(mppFrame);
        if ((pixelFormat & MPP_FRAME_FMT_MASK) != MPP_FMT_YUV420SP) {
            av_log(NULL, AV_LOG_ERROR, MPP_LOG_PREFIX "不支持的输出格式 0x%x（只支持 8 bit NV12）\n", pixelFormat);
            mpp_frame_deinit(&mppFrame);
            return AVERROR_PATCHWELCOME;
        }
        return wrapMppFrameAsAvFrame(decoder, mppFrame, frame);
    }
}

int mppVideoDecoderIsWaitingForOutput(const MppVideoDecoder *decoder) {
    if (decoder->hasReachedEndOfStream) {
        return 0;
    }
    return decoder->hasPendingAnnexbPacket || decoder->hasPendingEndOfStream || decoder->hasSentEndOfStream;
}

void mppVideoDecoderFlush(MppVideoDecoder *decoder) {
    // MppApi::reset：丢掉所有还没解的码流和还没取走的帧，解码器回到“等关键帧”的状态；
    // 内存池和宽高设置保留，不用重新 info change。已经交出去的帧不受影响
    decoder->decoderApi->reset(decoder->decoderCtx);
    av_bsf_flush(decoder->annexbFilterCtx); // 过滤器里如果留着包，也一起丢掉
    av_packet_unref(decoder->annexbPacket);
    decoder->hasPendingAnnexbPacket = 0;
    decoder->hasPendingEndOfStream = 0;
    decoder->hasSentEndOfStream = 0;
    decoder->hasReachedEndOfStream = 0;
}

void mppVideoDecoderRelease(MppVideoDecoder **decoderPointer) {
    if (!decoderPointer || !*decoderPointer) {
        return;
    }
    // 先把引用拿到局部变量里再 unref：如果这是最后一份引用，unref 会把 decoder 整个释放掉，
    // 之后不能再访问 decoder->selfRef
    AVBufferRef *decoderRef = (*decoderPointer)->selfRef;
    (*decoderPointer)->selfRef = NULL;
    *decoderPointer = NULL;
    av_buffer_unref(&decoderRef);
}
