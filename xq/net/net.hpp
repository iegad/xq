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

#include "third/blockingconcurrentqueue.h"
#include "tools/tools.hpp"

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif // !INVALID_SOCKET

namespace xq {
namespace net {

constexpr size_t   KCP_WND = 512;
constexpr size_t   KCP_MTU = 1418;
constexpr size_t   KCP_MAX_DATA_SIZE = 1418 * 128;
constexpr size_t   KCP_HEAD_SIZE = 24;
constexpr uint32_t KCP_DEFAULT_TIMEOUT = 60000;
constexpr int64_t  KCP_UPDATE_MS = 20;

constexpr uint32_t IO_RCVBUF_SIZE = 1024 * 1024 * 32;
constexpr int      IO_BLOCK_SIZE = 16;
constexpr int      IO_MSG_SIZE = 256;

#ifdef _WIN32
    typedef SOCKET SOCKET;
#else
    typedef int SOCKET;
#endif

class KcpSess;

struct RxSeg {
    static xq::tools::ObjectPool<RxSeg>* pool() {
        return xq::tools::ObjectPool<RxSeg>::Instance();
    }

    int len;
    socklen_t addrlen;
    sockaddr addr;
    uint8_t data[KCP_MTU * IO_BLOCK_SIZE];
    KcpSess *sess;

    explicit RxSeg()
        : len(KCP_MTU* IO_BLOCK_SIZE)
        , addrlen(sizeof(addr))
        , addr({ 0,{0} }) {
        assert(data);
    }
}; // struct RxSeg;

struct TxSeg {
    static xq::tools::ObjectPool<TxSeg>* pool() {
        return xq::tools::ObjectPool<TxSeg>::Instance();
    }

    int len;
    uint8_t data[KCP_MTU];

    explicit TxSeg()
        : len(KCP_MTU) {
        assert(data);
    }
}; // struct TxSeg

typedef moodycamel::BlockingConcurrentQueue<RxSeg*> RxQueue;
typedef moodycamel::BlockingConcurrentQueue<TxSeg*> TxQueue;

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
/// 创建UDP套接字
/// </summary>
/// <param name="local">本地Endpoint</param>
/// <param name="remote">远端Endpoint</param>
/// <returns>成功返回 套接字描述符, 否则返回 INVALID_SOCKET</returns>
SOCKET udp_socket(const char* local, const char* remote, sockaddr *addr = nullptr, socklen_t *addrlen = nullptr) {
    static constexpr int ON = 1;

    SOCKET fd = INVALID_SOCKET;

    // 本端地址用于udp 套接字绑定
    if (local) {
        std::string tmp = std::string(local);
        int pos = (int)tmp.rfind(':');
        if (pos == -1)
            return INVALID_SOCKET;

        std::string ip = tmp.substr(0, pos);
        if (ip.empty())
            ip = "0.0.0.0";

        std::string port = tmp.substr(pos + 1);

        addrinfo hints;
        addrinfo* result = nullptr, * rp = nullptr;

        ::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result))
            return INVALID_SOCKET;

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == INVALID_SOCKET)
                continue;

            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
                close(fd);
                return INVALID_SOCKET;
            }

#ifndef _WIN32
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
                close(fd);
                return INVALID_SOCKET;
            }
#endif

            if (!::bind(fd, rp->ai_addr, (int)rp->ai_addrlen))
                break;

            close(fd);
            fd = INVALID_SOCKET;
        }

        assert(rp);

        ::freeaddrinfo(result);

        if (fd == INVALID_SOCKET)
            return INVALID_SOCKET;
    }

    // 对端地址用于连接
    if (remote) {
        std::string tmp = std::string(remote);
        int pos = (int)tmp.rfind(':');
        if (pos == -1) {
            close(fd);
            return INVALID_SOCKET;
        }

        std::string ip = tmp.substr(0, pos);
        if (ip.empty()) {
            close(fd);
            return INVALID_SOCKET;
        }

        std::string port = tmp.substr(pos + 1);

        addrinfo hints;
        addrinfo* result = nullptr, * rp = nullptr;

        ::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result)) {
            close(fd);
            return INVALID_SOCKET;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            if (fd == INVALID_SOCKET) {
                fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (fd == INVALID_SOCKET)
                    continue;

                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
                    close(fd);
                    return INVALID_SOCKET;
                }

#ifndef _WIN32
                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
                    close(fd);
                    return INVALID_SOCKET;
                }
#endif
            }

            if (!::connect(fd, rp->ai_addr, rp->ai_addrlen)) {
                assert(addr && addrlen);
                *addrlen = rp->ai_addrlen;
                ::memcpy(addr, rp->ai_addr, rp->ai_addrlen);
                break;
            }

            close(fd);
            fd = INVALID_SOCKET;
        }

        assert(rp);

        ::freeaddrinfo(result);

        if (fd == INVALID_SOCKET)
            return INVALID_SOCKET;
    }

    return fd;
}

std::string addr2str(const sockaddr *addr) {
    std::string rzt;
    char buf[38] = { 0 };

    switch (addr->sa_family) {

    case AF_INET: {
        sockaddr_in* ra = (sockaddr_in*)addr;
        assert(::inet_ntop(AF_INET, &ra->sin_addr, buf, sizeof(buf)) && "inet_ntop failed");
        rzt = std::string(buf) + ":" + std::to_string(ntohs(ra->sin_port));
    } break;

    case AF_INET6: {
        sockaddr_in6* ra = (sockaddr_in6*)addr;
        assert(::inet_ntop(AF_INET6, &ra->sin6_addr, buf, sizeof(buf)) && "inet_ntop failed");
        rzt = std::string(buf) + ":" + std::to_string(ntohs(ra->sin6_port));
    } break;
    } // switch (addr_.sa_family);

    return rzt;
}

} // namespace net
} // namespace xq

#endif // __NET_HPP__
