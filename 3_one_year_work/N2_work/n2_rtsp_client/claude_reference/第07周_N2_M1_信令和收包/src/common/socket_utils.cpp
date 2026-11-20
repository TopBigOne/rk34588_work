#include "common/socket_utils.h"

#include <cerrno>       // errno
#include <cstring>      // strerror、memset
#include <netdb.h>      // getaddrinfo、freeaddrinfo、gai_strerror
#include <sys/socket.h> // socket、connect、send、setsockopt
#include <sys/time.h>   // timeval
#include <unistd.h>     // close

#include "common/log.h"

static const char *const kTag = "Socket";

/**
 * @brief 把毫秒换成 setsockopt 要的 timeval（秒 + 微秒）
 * @param[in] timeoutMs  毫秒
 * @return 对应的 timeval
 * @note 例：1500 ms → tv_sec = 1，tv_usec = 500000
 */
static timeval makeTimeval(int timeoutMs) {
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    return timeout;
}

/**
 * @brief 用 TCP 连接 host:port
 * @param[in] host             主机名或 IP
 * @param[in] port             端口
 * @param[in] connectTimeoutMs connect 超时，毫秒
 * @return socket fd，失败 -1
 * @note 第 1 步 getaddrinfo 把主机名解析成地址；第 2 步 逐个地址 socket + connect；第 3 步 释放地址链表
 */
int connectTcp(const std::string &host, int port, int connectTimeoutMs) {
    // 第 1 步 解析地址。hints 告诉 getaddrinfo 我们要什么样的地址：
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4、IPv6 都行
    hints.ai_socktype = SOCK_STREAM; // 流式 socket = TCP
    addrinfo *addressList = nullptr; // getaddrinfo 分配的地址链表，用完要 freeaddrinfo
    const std::string portText = std::to_string(port);
    // getaddrinfo 的返回值不是 -1/errno 那一套：0 = 成功，非 0 是错误码，要用 gai_strerror 翻译
    const int resolveRet = getaddrinfo(host.c_str(), portText.c_str(), &hints, &addressList);
    if (resolveRet != 0) {
        LOGE(kTag, "解析 %s 失败：%s", host.c_str(), gai_strerror(resolveRet));
        return -1;
    }

    // 第 2 步 逐个地址尝试
    int connectedFd = -1;
    for (addrinfo *address = addressList; address != nullptr; address = address->ai_next) {
        // socket(地址族, 类型, 协议)：创建一个还没连接的 socket，返回 fd；失败返回 -1
        const int socketFd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socketFd < 0) {
            continue;
        }
        // SO_SNDTIMEO：发送超时。Linux 上 connect 也受它限制，连不上的地址不会卡很久（推断，见头文件）
        const timeval timeout = makeTimeval(connectTimeoutMs);
        setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        // connect：发起 TCP 三次握手，阻塞到握手完成或失败。成功返回 0
        if (connect(socketFd, address->ai_addr, address->ai_addrlen) == 0) {
            connectedFd = socketFd;
            break;
        }
        LOGW(kTag, "connect %s:%d 失败：%s", host.c_str(), port, strerror(errno));
        close(socketFd); // 这个地址连不上，关掉换下一个
    }

    // 第 3 步 释放 getaddrinfo 分配的链表
    freeaddrinfo(addressList);
    return connectedFd;
}

/**
 * @brief 把数据全部发出去
 * @param[in] socketFd   socket
 * @param[in] data       数据
 * @param[in] sizeBytes  字节数
 * @return true = 发完；false = 出错
 * @note send 返回实际发出的字节数，可能比要发的少（内核发送缓冲区满了），所以要循环发剩下的
 */
bool sendAll(int socketFd, const void *data, size_t sizeBytes) {
    const char *remainingData = static_cast<const char *>(data);
    size_t remainingBytes = sizeBytes;
    while (remainingBytes > 0) {
        // send(fd, 数据, 字节数, 标志)：MSG_NOSIGNAL = 对方断开时不发 SIGPIPE 信号，只返回 -1
        const ssize_t sentBytes = send(socketFd, remainingData, remainingBytes, MSG_NOSIGNAL);
        if (sentBytes < 0) {
            if (errno == EINTR) {
                continue; // 被信号打断，还没发出任何字节，重发
            }
            LOGE(kTag, "send 失败：%s", strerror(errno));
            return false;
        }
        remainingData += sentBytes;
        remainingBytes -= static_cast<size_t>(sentBytes);
    }
    return true;
}

/**
 * @brief 设置接收超时
 * @param[in] socketFd   socket
 * @param[in] timeoutMs  毫秒，0 = 永不超时
 * @return true = 成功
 * @note setsockopt(fd, 层级, 选项名, 选项值地址, 选项值大小)：SOL_SOCKET 层级的 SO_RCVTIMEO 选项
 */
bool setReceiveTimeout(int socketFd, int timeoutMs) {
    const timeval timeout = makeTimeval(timeoutMs);
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        LOGE(kTag, "设置 SO_RCVTIMEO 失败：%s", strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief 判断 errno 是不是超时
 * @param[in] savedErrno  保存的 errno
 * @return true = 超时
 * @note 见头文件
 */
bool isTimeoutErrno(int savedErrno) {
    return savedErrno == EAGAIN || savedErrno == EWOULDBLOCK;
}
