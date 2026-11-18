//
// day8_qt_player：Qt 播放器，播 mp4。视频 MPP 硬解 + RGA 转格式缩放，声音 ffmpeg 软解 + ALSA 播放
//
// 用法：
//   day8_qt_player <文件.mp4> [--no-audio]          播放器（--no-audio = 只播画面，M2）
//   day8_qt_player m1 <文件.mp4> <输出.nv12> [帧数]  M1：只解视频，存成 NV12 文件，用来比 md5
//   day8_qt_player m3 <文件.mp4> [输出.pcm]          M3：只播声音（可以同时存 PCM），用来听、比 md5
//
// 环境变量：
//   PLAYER_ALSA_DEVICE=plughw:1,0   指定声卡（默认 "default"）
//
#include <QApplication>
#include <cstdio>  // printf
#include <cstdlib> // atoi
#include <cstring> // strcmp

#include "app/m1_decode_video_to_nv12.h"
#include "app/m3_play_audio_only.h"
#include "ui/player_window.h"

/**
 * @brief 打印用法
 * @return 无
 * @note 参数不对时调用
 */
static void printUsage() {
    printf("用法：\n"
           "  day8_qt_player <文件.mp4> [--no-audio]\n"
           "  day8_qt_player m1 <文件.mp4> <输出.nv12> [帧数]\n"
           "  day8_qt_player m3 <文件.mp4> [输出.pcm]\n");
}

/**
 * @brief 程序入口：m1 / m3 走命令行验证，其他情况打开播放器窗口
 * @param[in] argc  参数个数（包括程序名）
 * @param[in] argv  参数
 * @return 0 = 正常退出；1 = 参数不对或运行失败
 * @note m1、m3 不创建 QApplication（不需要窗口，也不用连 weston）。
 *       播放器模式下 QApplication 会先吃掉 Qt 自己认识的参数（例如 -platform wayland），所以在它之后再读 argv
 */
int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "m1") == 0) {
        if (argc < 4) {
            printUsage();
            return 1;
        }
        const int maxFrameCount = argc >= 5 ? atoi(argv[4]) : 0; // atoi：字符串转 int
        return runM1DecodeVideoToNv12(argv[2], argv[3], maxFrameCount);
    }
    if (argc >= 2 && strcmp(argv[1], "m3") == 0) {
        if (argc < 3) {
            printUsage();
            return 1;
        }
        return runM3PlayAudioOnly(argv[2], argc >= 4 ? argv[3] : nullptr);
    }

    // 播放器：QApplication 负责连上显示服务（weston），每个 Widgets 程序有且只有一个
    QApplication app(argc, argv);
    if (argc < 2) {
        printUsage();
        return 1;
    }
    const bool shouldPlayAudio = !(argc >= 3 && strcmp(argv[2], "--no-audio") == 0);

    PlayerWindow playerWindow;
    // showFullScreen：铺满整个屏幕，盖住 weston 的任务栏；退出用界面上的「退出」按钮
    playerWindow.showFullScreen();
    // QString::fromLocal8Bit：命令行参数是本地编码（UTF-8）的字节串，转成 QString
    playerWindow.openFile(QString::fromLocal8Bit(argv[1]), shouldPlayAudio);
    // exec()：进入事件循环，quit() 被调用（点了退出）后返回
    return app.exec();
}
