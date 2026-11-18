//
// day8_qt_player（界面上的名字叫 TOnePlayer）：Qt 播放器，播 mp4。视频 MPP 硬解 + RGA 转格式缩放，声音 ffmpeg 软解 + ALSA 播放
//
// 用法：
//   day8_qt_player [文件.mp4] [--no-audio]          播放器（不写文件 = 播 kDefaultMediaPath；--no-audio = 只播画面，M2）
//   day8_qt_player m1 <文件.mp4> <输出.nv12> [帧数]  M1：只解视频，存成 NV12 文件，用来比 md5
//   day8_qt_player m3 <文件.mp4> [输出.pcm]          M3：只播声音（可以同时存 PCM），用来听、比 md5
//
// 环境变量：
//   PLAYER_ALSA_DEVICE=plughw:1,0   指定声卡（默认 "default"）
//   PLAYER_SCREENSHOT_PATH=/tmp/a.png  调试用：播放器启动 3 秒后把窗口截图存到这里
//   SYSTEMUI_APP_NAME=toneplayer    从正点原子桌面启动时由 board/start_toneplayer.sh 设置：设了才请桌面藏起来
//
#include <QApplication>
#include <QTimer>
#include <cstdio>  // printf
#include <cstdlib> // atoi、getenv
#include <cstring> // strcmp
#include <memory>  // std::unique_ptr

#include "app/m1_decode_video_to_nv12.h"
#include "app/m3_play_audio_only.h"
#include "ui/player_window.h"
#include "ui/systemui_client.h"

// 不带参数启动时播的视频：在正点原子桌面上点 TOnePlayer 图标时没有参数，就播它。
// 放在 /userdata：数据分区，板子重启不会清空（/tmp 会）
static const char *const kDefaultMediaPath = "/userdata/day8_media/yequ_640x480.mp4";

/**
 * @brief 打印用法
 * @return 无
 * @note 参数不对时调用
 */
static void printUsage() {
    printf("用法：\n"
           "  day8_qt_player [文件.mp4] [--no-audio]\n"
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
    // setApplicationName：程序的名字，Qt 用它当窗口标题的默认值（Wayland 下 weston 也用它认窗口）
    QApplication::setApplicationName(QStringLiteral("TOnePlayer"));
    const char *mediaPath = argc >= 2 ? argv[1] : kDefaultMediaPath;
    const bool shouldPlayAudio = !(argc >= 3 && strcmp(argv[2], "--no-audio") == 0);

    PlayerWindow playerWindow;
    playerWindow.setWindowTitle(QStringLiteral("TOnePlayer"));
    // showFullScreen：铺满整个屏幕，盖住 weston 的任务栏；退出用界面上的「退出」按钮
    playerWindow.showFullScreen();
    // QString::fromLocal8Bit：命令行参数是本地编码（UTF-8）的字节串，转成 QString
    playerWindow.openFile(QString::fromLocal8Bit(mediaPath), shouldPlayAudio);

    // 调试用：设了环境变量 PLAYER_SCREENSHOT_PATH，就在 3 秒后把整个窗口存成图片。
    // 板子上的 weston 不是 --debug 启动的，系统截屏工具 weston-screenshooter 会报 unauthorized，
    // 而重启 weston 会把正点原子桌面 systemui 一起关掉，所以让程序自己截。
    // QWidget::grab()：让窗口把自己（连同所有子控件）画到一张 QPixmap 上，不经过 weston
    const char *screenshotPath = getenv("PLAYER_SCREENSHOT_PATH"); // getenv：读环境变量，没设时返回 nullptr
    if (screenshotPath) {
        const QString screenshotFilePath = QString::fromLocal8Bit(screenshotPath);
        // QTimer::singleShot(毫秒, 接收对象, 函数)：过一段时间在界面线程里调用一次函数
        QTimer::singleShot(3000, &playerWindow, [&playerWindow, screenshotFilePath]() {
            const bool isSaved = playerWindow.grab().save(screenshotFilePath); // save：按扩展名选格式（.png）
            printf("截图 %s：%s\n", isSaved ? "成功" : "失败", screenshotFilePath.toLocal8Bit().constData());
            fflush(stdout); // adb shell 的输出有缓冲，立刻刷出去
        });
    }
    // 从正点原子桌面图标启动时（启动脚本设了 SYSTEMUI_APP_NAME），请桌面 systemui 藏起来，不然约 1 秒后它的全屏窗口
    // 会盖住播放器（见 ui/systemui_client.h）。用 run 脚本从 adb 启动时不设：systemui 不知道有这个程序，
    // 藏起来以后程序退出它也不会自己回来，屏幕就空了
    const char *systemUiAppName = getenv("SYSTEMUI_APP_NAME");
    std::unique_ptr<SystemUiClient> systemUiClientPtr; // 不从桌面启动时是空的
    if (systemUiAppName) {
        systemUiClientPtr = std::make_unique<SystemUiClient>(QString::fromLocal8Bit(systemUiAppName));
        systemUiClientPtr->requestHideDesktop();
    }

    // exec()：进入事件循环，quit() 被调用（点了退出）后返回
    const int exitCode = app.exec();
    // 先停播放（声音立刻停、声卡关掉），再请桌面显示回来。以前顺序反了：请桌面显示后还要转 300 ms 事件循环，
    // 这段时间声音一直在播（2026-11-19 用户反馈"退出后喇叭还有闷闷的声音"）
    playerWindow.stopPlayback();
    if (systemUiClientPtr) {
        systemUiClientPtr->requestShowDesktopBeforeExit(); // 退出前请桌面重新显示
    }
    return exitCode;
}
