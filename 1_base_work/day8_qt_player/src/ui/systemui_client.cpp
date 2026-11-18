#include "ui/systemui_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRemoteObjectDynamicReplica>
#include <QUrl>

#include "common/log.h"

static const char *const kTag = "SystemUiClient";

/**
 * @brief 记下程序名并连上 systemui
 * @param[in] requestedAppName  程序名
 * @param[in] parent            Qt 父对象
 * @note 第 1 步 connectToNode：连上 systemui 的节点；第 2 步 acquireDynamic：要一个 SystemUICommonApi 的副本；
 *       第 3 步 接上"副本准备好了"和"systemui 发来消息"两个信号
 */
SystemUiClient::SystemUiClient(const QString &requestedAppName, QObject *parent)
    : QObject(parent), appName(requestedAppName) {
    // 第 1 步 connectToNode(地址)：只是发起连接，返回 false = 地址格式不对（连不连得上要等之后的信号）
    if (!remoteNode.connectToNode(QUrl(QString::fromLatin1(kHostUrl)))) {
        LOGW(kTag, "connectToNode %s 失败", kHostUrl);
        return;
    }
    // 第 2 步 acquireDynamic(对象名)：返回一个副本对象，这时它还是空的，连上并收到对方的方法列表后才"初始化好"
    replica = remoteNode.acquireDynamic(QString::fromLatin1(kSourceName));

    // 第 3 步 initialized：副本拿到了对方的方法、信号列表，可以调用了
    connect(replica, &QRemoteObjectDynamicReplica::initialized, this, &SystemUiClient::handleReplicaInitialized);
}

/**
 * @brief 断开和副本的连接、删掉副本
 * @note 析构函数体在成员（remoteNode）析构之前执行，所以在这里删副本，node 还活着
 */
SystemUiClient::~SystemUiClient() {
    if (replica) {
        disconnect(replica, nullptr, this, nullptr); // 断开副本发给本对象的所有信号（nullptr = 不限信号、不限槽）
        delete replica;
        replica = nullptr;
    }
}

/**
 * @brief 请 systemui 把桌面藏起来
 * @return 无
 * @note 还没连上时先记下来，handleReplicaInitialized 里补发
 */
void SystemUiClient::requestHideDesktop() {
    if (!sendCommand(kCommandHide)) {
        shouldHideWhenReady = true;
    }
}

/**
 * @brief 请 systemui 把桌面重新显示出来，并等最多 300 ms 让请求真正发出去
 * @return 无
 * @note invokeMethod 只是把请求排进发送队列，真正写进套接字要靠事件循环。
 *       QEventLoop::processEvents 在这里自己处理一会儿事件，300 ms 足够把一条很短的消息送出去
 */
void SystemUiClient::requestShowDesktopBeforeExit() {
    if (!sendCommand(kCommandShow)) {
        return;
    }
    QEventLoop eventLoop;
    QElapsedTimer elapsedTimer; // 计时器：elapsed() 返回从 start() 到现在的毫秒数
    elapsedTimer.start();
    while (elapsedTimer.elapsed() < 300) {
        eventLoop.processEvents(QEventLoop::AllEvents, 50); // 处理事件，最多处理 50 ms 就返回
    }
}

/**
 * @brief 拿到 systemui 的对象了
 * @return 无
 * @note 第 1 步 接上 systemui 的 serverSendVariant 信号；第 2 步 之前要求过藏桌面就现在发
 */
void SystemUiClient::handleReplicaInitialized() {
    LOGI(kTag, "连上 systemui（%s，%s），程序名 %s", kHostUrl, kSourceName, appName.toLocal8Bit().constData());
    // 第 1 步 动态副本的信号编译时不知道，只能用字符串形式的 SIGNAL()/SLOT() 宏按名字连接
    connect(replica, SIGNAL(serverSendVariant(QString, int, int, QVariant)), this,
            SLOT(handleServerVariant(QString, int, int, QVariant)));

    // 第 2 步 补发
    if (shouldHideWhenReady) {
        shouldHideWhenReady = false;
        sendCommand(kCommandHide);
    }
}

/**
 * @brief systemui 发来的消息：回答"还在吗"，响应"退出"
 * @param[in] senderAppName  程序名
 * @param[in] type           类型
 * @param[in] command        命令
 * @param[in] variant        附带数据
 * @return 无
 * @note 第 1 步 不是发给我们的不管；第 2 步 IsActive → 回一条 IsActive；第 3 步 Quit → 退出程序；其他的打日志
 */
void SystemUiClient::handleServerVariant(const QString &senderAppName, int type, int command,
                                         const QVariant &variant) {
    // 第 1 步 systemui 是广播的，所有连上的程序都会收到
    if (senderAppName != appName) {
        return;
    }
    // 第 2 步 心跳：约每 2 秒一次，回答"还在"。很频繁，不打日志
    if (type == kTypeActivateControl && command == kCommandIsActive) {
        sendCommand(kCommandIsActive);
        return;
    }
    // 第 3 步 systemui 要求退出（例如它要切到别的程序）：quit() 让 main 里的 exec() 返回，正常走退出流程
    if (type == kTypeActivateControl && command == kCommandQuit) {
        LOGI(kTag, "systemui 要求退出");
        QCoreApplication::quit();
        return;
    }
    // toString()：QVariant 里是字符串、数字时转成文字，别的类型返回空字符串
    LOGI(kTag, "systemui 发来：类型 %d，命令 %d，数据 %s", type, command,
         variant.toString().toLocal8Bit().constData());
}

/**
 * @brief 调用 systemui 的 onServerVariant(程序名, 类型, 命令, 空数据)
 * @param[in] command  命令值（kCommandHide / kCommandShow / kCommandIsActive）
 * @return true = 请求已交给 Qt Remote Objects；false = 还没连上或调用失败
 * @note isInitialized()：副本是否已经拿到对方的方法列表；没拿到时按名字找不到方法，调用会失败。
 *       QMetaObject::invokeMethod(对象, "方法名", Q_ARG(类型, 值)...)：按名字调用方法，
 *       对副本来说就是把调用打包发给 systemui，在 systemui 进程里执行
 */
bool SystemUiClient::sendCommand(int command) {
    if (!replica || !replica->isInitialized()) {
        return false;
    }
    const bool isInvoked = QMetaObject::invokeMethod(replica, "onServerVariant", Q_ARG(QString, appName),
                                                     Q_ARG(int, kTypeActivateControl), Q_ARG(int, command),
                                                     Q_ARG(QVariant, QVariant()));
    if (command != kCommandIsActive) { // 心跳太频繁，不打日志
        LOGI(kTag, "发给 systemui：命令 %d（1 = 显示桌面，5 = 隐藏桌面）%s", command, isInvoked ? "已发出" : "失败");
    }
    return isInvoked;
}
