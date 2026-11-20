/*
 * socket 小工具：连服务器、把一段数据完整发出去、设置接收超时、关 socket。
 * N1 计划里服务端也有一份 socket_utils（sendAll、忽略 SIGPIPE）；N1 的代码还没写出来，这里按同样的名字自己写一份。
 */
#pragma once

#include <cstddef> // size_t
#include <string>

/**
 * @brief 用 TCP 连接 host:port，成功返回 socket 文件描述符
 * @param[in] host            主机名或 IP，例如 "127.0.0.1"
 * @param[in] port            端口，例如 8554
 * @param[in] connectTimeoutMs connect 最多等多少毫秒（用 SO_SNDTIMEO 实现）
 * @return >= 0：连好的 socket fd，调用方负责 close；-1：失败（已打印原因）
 * @note 只连 IPv4/IPv6 里 getaddrinfo 返回的第一个能连上的地址。
 *       Linux 上 SO_SNDTIMEO 对 connect 也生效（超时返回 EINPROGRESS）：推断，没在板子上实测
 */
int connectTcp(const std::string &host, int port, int connectTimeoutMs);

/**
 * @brief 把 sizeBytes 个字节全部发出去（send 可能一次只发一部分，要循环）
 * @param[in] socketFd   已连接的 socket
 * @param[in] data       要发的数据
 * @param[in] sizeBytes  字节数
 * @return true = 全部发完；false = 出错（对方断开、超时等，已打印原因）
 * @note 用 MSG_NOSIGNAL：对方已经关闭连接时 send 返回 -1（errno = EPIPE），而不是让内核发 SIGPIPE 把整个程序杀掉
 */
bool sendAll(int socketFd, const void *data, size_t sizeBytes);

/**
 * @brief 设置接收超时：recv 最多阻塞 timeoutMs 毫秒，超时返回 -1，errno = EAGAIN 或 EWOULDBLOCK
 * @param[in] socketFd   socket
 * @param[in] timeoutMs  毫秒；0 = 永不超时
 * @return true = 设置成功；false = 失败
 * @note 用 setsockopt(SO_RCVTIMEO)，见 man 7 socket
 */
bool setReceiveTimeout(int socketFd, int timeoutMs);

/**
 * @brief 判断 recv 返回 -1 时，是不是"超时"（而不是真的出错）
 * @param[in] savedErrno  recv 返回 -1 后立刻保存的 errno
 * @return true = 超时；false = 别的错误
 * @note 设了 SO_RCVTIMEO 后超时的 errno 是 EAGAIN 或 EWOULDBLOCK（Linux 上两个值相同，Mac 上也相同，写两个是为了可移植）
 */
bool isTimeoutErrno(int savedErrno);
