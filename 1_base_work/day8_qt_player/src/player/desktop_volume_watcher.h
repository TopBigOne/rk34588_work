#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

/**
 * @brief 盯着正点原子桌面的音量文件，音量变了就发信号
 * @note 板子上按音量 +、- 键时，正点原子桌面的 sysvolume 程序弹出音量条，并把 0～100 的数字写进
 *       /opt/ui/src/apps/resource/volume（2026-11-19 实测：按键前后所有声卡的音量控件、PulseAudio 的音量都没变，
 *       只有这个文件变了）。它不改系统音量，正点原子自己的 music、player 等程序都是读这个文件、自己调音量。
 *       我们的播放器也照这个约定来：读这个数字，由音频线程把采样按比例缩小。
 *       文件不存在（不是正点原子的桌面）时，音量按 100 算，也就是不缩小
 */
class DesktopVolumeWatcher : public QObject {
    Q_OBJECT // 有自己的信号，要让 moc 生成信号的实现代码

public:
    /**
     * @brief 开始监听音量文件
     * @param[in] parent  Qt 父对象
     * @note 文件不存在时不监听，readVolumePercent() 一直返回 100
     */
    explicit DesktopVolumeWatcher(QObject *parent = nullptr);

    /**
     * @brief 读一次音量文件
     * @return 0～100 的音量百分比；文件不存在或内容不是数字时返回 100
     * @note 超出 0～100 的数字会被截到这个范围里
     */
    int readVolumePercent() const;

signals:
    /**
     * @brief 音量文件的内容变了
     * @param[in] volumePercent  新的音量百分比，0～100
     * @note 在界面线程（创建本对象的线程）里发出
     */
    void volumeChanged(int volumePercent);

private:
    /**
     * @brief QFileSystemWatcher 报告文件变了：重新读，数字真的变了才发 volumeChanged
     * @param[in] changedFilePath  变化的文件路径
     * @return 无
     * @note 有的程序写文件是"写一个新文件再改名盖掉旧的"，旧文件被盖掉后 QFileSystemWatcher 就不再盯它了，
     *       所以每次都检查一下，不在监听列表里就重新加进去
     */
    void handleFileChanged(const QString &changedFilePath);

    QFileSystemWatcher fileWatcher;   // Qt 的文件监听：文件被改写时发 fileChanged 信号（内部用 Linux 的 inotify）
    int lastVolumePercent = 100;      // 上一次读到的音量，用来判断是不是真的变了

    static constexpr const char *kVolumeFilePath = "/opt/ui/src/apps/resource/volume"; // 正点原子桌面的音量文件
};
