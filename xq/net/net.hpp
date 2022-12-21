#ifndef __NET_HPP__
#define __NET_HPP__

//! --------------------------------------------------------------------------------------------------------------------
//! xq 网络框架, 目前只支持KCP版本.
//! 
//! @author:      iegad
//! @create at:   2022-12-06
//! @update:
//! --------------------------------------------------------------------------------------------------------------------
//! |- time                |- coder                  |- content 


#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <jemalloc/jemalloc.h>
#endif

#include <assert.h>
#include <errno.h>
#include <memory.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "third/ikcp.h"
#include "tools/tools.hpp"

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif // !INVALID_SOCKET

namespace xq {
namespace net {

#ifdef _WIN32
    typedef SOCKET SOCKET;
#else
    typedef int SOCKET;
#endif

constexpr size_t   KCP_MTU             = 1418;
constexpr size_t   KCP_MAX_DATA_SIZE   = 1418 * 128;
constexpr size_t   KCP_HEAD_SIZE       = 24;
constexpr uint32_t KCP_DEFAULT_TIMEOUT = 60;
constexpr int64_t  KCP_UPDATE_MS       = 20;

enum class ErrType {
    ET_ListenerRead = 0,
    ET_ListenerWrite,
    ET_SessRead,
};

/// <summary>
/// 关闭套接字
/// </summary>
/// <param name="sockfd">需要关闭的套接字</param>
/// <returns>成功返回0, 否则返回-1</returns>
inline int close(SOCKET sockfd) {
    if (sockfd == INVALID_SOCKET)
        return 0;

#ifndef _WIN32
    return ::close(sockfd);
#else
    return ::closesocket(sockfd);
#endif
}

/// <summary>
/// 获取socket 最后一次错误码.
/// </summary>
/// <returns>错误码</returns>
inline int error() {
#ifndef _WIN32
    return errno;
#else
    return WSAGetLastError();
#endif
}

/// <summary>
/// 创建UDP套接字, 如果is_server 参数为真, 将会以服务端方式绑定 Endpoint(ip:svc), 否则将会以客户端连接 Endpoint(ip:svc)
/// </summary>
/// <param name="local">本地Endpoint</param>
/// <param name="remote">远端Endpoint</param>
/// <returns>成功返回 套接字描述符, 否则返回 INVALID_SOCKET</returns>
SOCKET udp_socket(const char* local, const char* remote, sockaddr *addr = nullptr, socklen_t *addrlen = nullptr);

} // namespace net
} // namespace xq

#endif // __NET_HPP__
