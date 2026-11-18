#include "player/desktop_volume_watcher.h"

#include <QFile>
#include <algorithm> // std::clamp

#include "common/log.h"

static const char *const kTag = "DesktopVolumeWatcher";

/**
 * @brief 开始监听音量文件
 * @param[in] parent  Qt 父对象
 * @note 第 1 步 读一次当前音量；第 2 步 把文件加进监听列表，并把 fileChanged 信号接到 handleFileChanged
 */
DesktopVolumeWatcher::DesktopVolumeWatcher(QObject *parent) : QObject(parent) {
    // 第 1 步 读一次，作为初始音量
    lastVolumePercent = readVolumePercent();

    // 第 2 步 监听。addPath 返回 false = 加不进去（一般是文件不存在）
    if (!fileWatcher.addPath(kVolumeFilePath)) {
        LOGW(kTag, "没有 %s（不是正点原子的桌面？），音量按 100%% 算，音量键不起作用", kVolumeFilePath);
        return;
    }
    // connect(发信号的对象, 信号, 接收对象, 槽)：文件变了，Qt 在界面线程里调用 handleFileChanged
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, &DesktopVolumeWatcher::handleFileChanged);
    LOGI(kTag, "监听 %s，当前音量 %d%%", kVolumeFilePath, lastVolumePercent);
}

/**
 * @brief 读一次音量文件
 * @return 0～100 的音量百分比；文件不存在或内容不是数字时返回 100
 * @note 文件内容是一个十进制数字，例如 "36"（2026-11-19 实测，后面没有换行）
 */
int DesktopVolumeWatcher::readVolumePercent() const {
    QFile volumeFile(kVolumeFilePath);
    // QIODevice::ReadOnly：只读打开；QIODevice::Text：按文本读（换行符统一成 \n）
    if (!volumeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 100;
    }
    // readAll()：读出全部内容（QByteArray）；trimmed()：去掉首尾的空格、换行；
    // toInt(&isNumber)：转成 int，转不了时 isNumber 是 false
    bool isNumber = false;
    const int volumePercent = volumeFile.readAll().trimmed().toInt(&isNumber);
    if (!isNumber) {
        return 100;
    }
    return std::clamp(volumePercent, 0, 100); // clamp(值, 下限, 上限)：截到 0～100
}

/**
 * @brief QFileSystemWatcher 报告文件变了：重新读，数字真的变了才发 volumeChanged
 * @param[in] changedFilePath  变化的文件路径
 * @return 无
 * @note 第 1 步 文件被"改名盖掉"时重新加进监听列表；第 2 步 重新读，变了才发信号
 */
void DesktopVolumeWatcher::handleFileChanged(const QString &changedFilePath) {
    // 第 1 步 files()：正在监听的文件列表。旧文件被盖掉后会自动从列表里消失，要重新加
    if (!fileWatcher.files().contains(changedFilePath) && QFile::exists(changedFilePath)) {
        fileWatcher.addPath(changedFilePath);
    }

    // 第 2 步 一次改写可能触发好几次 fileChanged，只在数字真的变了时才往外报
    const int volumePercent = readVolumePercent();
    if (volumePercent == lastVolumePercent) {
        return;
    }
    lastVolumePercent = volumePercent;
    LOGI(kTag, "音量变成 %d%%", volumePercent);
    emit volumeChanged(volumePercent); // emit：发信号，接上的槽会被调用
}
