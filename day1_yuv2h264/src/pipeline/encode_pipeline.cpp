/*
 * EncodePipeline 的实现，说明见 encode_pipeline.h
 */
#include "pipeline/encode_pipeline.h"

#include <cstdint> // uint8_t
#include <cstdio> // printf / perror
#include <vector>

bool EncodePipeline::run() {
    // ④ 输入缓冲区的排布要和编码器一致，所以 stride 从编码器拿
    if (!source_.prepare(encoder_.hor_stride(), encoder_.ver_stride())) {
        return false;
    }
    if (!write_header()) {
        return false;
    }
    return encode_all_frames();
}

bool EncodePipeline::write_header() {
    std::vector<uint8_t> header;
    if (!encoder_.get_header(header)) {
        return false;
    }
    if (!sink_.write(header)) {
        perror("fwrite header"); // 比如板子磁盘满了
        return false;
    }
    stats_.add_header(header.size()); // 总字节数要算上文件头，才和输出文件大小一致
    printf("|            header : %zu bytes\n", header.size());
    return true;
}

bool EncodePipeline::encode_all_frames() {
    MppBuffer frameBuffer = nullptr; // 这一帧图像所在的硬件缓冲区（由 source_ 提供）
    bool inputEos         = false; // M4: 输入读完了（我们告诉编码器："没有图像了"）
    bool outputEos        = false; // M4: 输出取完了（编码器告诉我们："码流全给你了"）
    EncodedPacket packet;

    // 结束条件是 outputEos（编码器说码流全给完了），不是 inputEos（我们读完了）：
    // 我们读完之后，编码器手里可能还有没吐出来的码流，要等它主动说"结束"才能退出
    stats_.start();
    while (!outputEos) {
        // 5.1 取一帧。读完以后就不再读，后面每次循环只送 EOS
        if (!inputEos) {
            inputEos = !source_.read_frame(frameBuffer);
            if (inputEos) {
                printf("              input end, send EOS\n");
            }
        }

        // 5.2 送一帧、取一包（MppFrame / MppPacket 的创建和释放都在 encode 里面；inputEos 时 frameBuffer 被忽略）
        if (!encoder_.encode(frameBuffer, inputEos, packet)) {
            return false;
        }
        outputEos = packet.eos;
        // 这次没取到码流，或者是 EOS 时的空包：不算一帧
        if (packet.data.empty()) {
            continue;
        }
        // 5.3 H.264 裸流（Annex-B）就是把每个 packet 原样首尾相接写进文件，不需要额外的文件头
        if (!sink_.write(packet.data)) {
            perror("fwrite frame"); // 比如板子磁盘满了
            return false;
        }
        printf("              frame %-3d size %zu bytes\n", stats_.frame_count(), packet.data.size());
        stats_.add_frame(packet.data.size(), packet.isKeyFrame);
    }
    stats_.stop();
    return true;
}
