#pragma once

#include <QObject>
#include <QRemoteObjectNode>
#include <QString>
#include <QVariant>

class QRemoteObjectDynamicReplica;

/**
 * @brief 和正点原子桌面 systemui 通信：请它把桌面藏起来 / 重新显示
 * @note 为什么需要：从桌面图标启动程序后，systemui 自己的全屏窗口约 1 秒后会盖回来，我们的播放器就"看不见了"，
 *       其实程序还在跑（2026-11-19 实测：点了 3 次图标，板子上同时有 3 个播放器进程）。
 *       正点原子自己的 music、player 等程序启动后都会请 systemui 藏起来，退出时再请它显示。
 *
 *       通信方式是 Qt Remote Objects（Qt 自带的跨进程调用：一个进程把对象"发布"出去，别的进程拿到它的副本，
 *       调副本的方法就等于调对方进程里的方法）。下面这些都是 2026-11-19 在板子上查到的：
 *         - 地址 local:interfaces（systemui 和 music 的二进制里都有这个字符串；local: = 本机的 Unix 域套接字）
 *         - 对象名 SystemUICommonApi，方法 onServerVariant(QString 程序名, int 类型, int 命令, QVariant 附带数据)
 *           （探针 acquireDynamic 拿到副本后打印 metaObject 得到）
 *         - 类型 ActivateControl = 1；命令 Show = 1、Quit = 2、Hide = 5
 *           （从 music 二进制里 moc 生成的枚举表读出来的）
 *         - 参数的顺序（类型在前、命令在后）是按名字推断的
 *       用"动态副本"（QRemoteObjectDynamicReplica）：不需要正点原子的 .rep 接口文件，连上以后才知道对方有哪些方法，
 *       用 QMetaObject::invokeMethod 按名字调用
 */
class SystemUiClient : public QObject {
    Q_OBJECT // 有自己的槽（接 systemui 发来的信号），要让 moc 处理

public:
    /**
     * @brief 记下程序名并连上 systemui
     * @param[in] requestedAppName  程序名：apk*.cfg 第 3 列、/opt/ui/src/apps/ 下的文件名（例如 "toneplayer"）
     * @param[in] parent            Qt 父对象
     * @note 连接是异步的：连上、拿到对象以后才真正发出藏桌面的请求（见 requestHideDesktop）
     */
    explicit SystemUiClient(const QString &requestedAppName, QObject *parent = nullptr);

    /**
     * @brief 断开和副本的连接、删掉副本
     * @note 必须在 remoteNode 析构之前删副本。成员按声明的逆序析构，remoteNode 会先于基类 QObject 析构；
     *       动态副本的元对象（记录它有哪些信号）跟着 node 一起没了，之后 ~QObject 断开"副本信号 → 我们的槽"时
     *       去查副本的元对象，就访问了已释放的内存 → 段错误（2026-11-19 上板 gdb 抓到：退出码 139，
     *       崩在 QObject::~QObject → QMetaObjectPrivate::signal）
     */
    ~SystemUiClient() override;

    /**
     * @brief 请 systemui 把桌面藏起来（我们的窗口就露出来了）
     * @return 无
     * @note 还没连上时先记下来，连上后自动发
     */
    void requestHideDesktop();

    /**
     * @brief 请 systemui 把桌面重新显示出来，并等最多 300 ms 让请求真正发出去
     * @return 无
     * @note 程序退出前调用（这时事件循环已经结束，所以这里自己转一会儿事件循环把消息送出去）
     */
    void requestShowDesktopBeforeExit();

private slots:
    /**
     * @brief 拿到 systemui 的对象了：如果之前要求过藏桌面，现在发出去
     * @return 无
     */
    void handleReplicaInitialized();

    /**
     * @brief systemui 发来的消息（signal serverSendVariant）：回答"还在吗"，响应"退出"
     * @param[in] senderAppName  程序名（systemui 是广播给所有连上的程序的，不是我们的名字就不管）
     * @param[in] type     类型（1 = ActivateControl ……）
     * @param[in] command  命令（1 = Show、2 = Quit、5 = Hide ……）
     * @param[in] variant  附带数据
     * @return 无
     * @note 2026-11-19 上板日志：发出隐藏请求后，systemui 先发 3 条（类型 3 Messages，命令 0，QString），
     *       之后约每 2 秒发一次（类型 1 ActivateControl，命令 4 IsActive）问"你还活着、在前台吗"。
     *       不回答的话约 3 秒后 systemui 认为程序不在了，把桌面显示回来盖住播放器（用户看到"播 3 秒自动退出"）。
     *       所以收到 IsActive 就回一条 IsActive；收到 Quit 就退出程序
     */
    void handleServerVariant(const QString &senderAppName, int type, int command, const QVariant &variant);

private:
    /**
     * @brief 调用 systemui 的 onServerVariant(程序名, 类型, 命令, 空数据)
     * @param[in] command  命令值（kCommandHide / kCommandShow / kCommandIsActive）
     * @return true = 请求已交给 Qt Remote Objects；false = 还没连上或调用失败
     */
    bool sendCommand(int command);

    QString appName;                                // 程序名
    QRemoteObjectNode remoteNode;                   // 本进程这一端的"节点"，负责和 systemui 的节点建立连接
    QRemoteObjectDynamicReplica *replica = nullptr; // systemui 那个 SystemUICommonApi 对象的副本（remoteNode 创建，我们负责 delete）
    bool shouldHideWhenReady = false;               // 还没连上就被要求藏桌面了，连上后补发

    static constexpr const char *kHostUrl = "local:interfaces";     // systemui 发布对象的地址
    static constexpr const char *kSourceName = "SystemUICommonApi"; // systemui 发布的对象名
    static constexpr int kTypeActivateControl = 1;                  // 类型：控制显示 / 隐藏
    static constexpr int kCommandShow = 1;                          // 命令：显示桌面
    static constexpr int kCommandQuit = 2;                          // 命令：（systemui 要求）程序退出
    static constexpr int kCommandIsActive = 4;                      // 命令：问"还在前台吗"/ 回答"还在"
    static constexpr int kCommandHide = 5;                          // 命令：隐藏桌面
};
