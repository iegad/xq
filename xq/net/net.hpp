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

constexpr size_t   KCP_WND = 512;                   // KCP 默认读/写窗口
constexpr size_t   KCP_MTU = 1418;                  // KCP 最大传输单元
constexpr size_t   KCP_MAX_DATA_SIZE = 1418 * 128;  // KCP 单包最大字节
constexpr size_t   KCP_HEAD_SIZE = 24;              // KCP 消息头长度
constexpr uint32_t KCP_DEFAULT_TIMEOUT = 60000;     // KCP 默认超时(毫秒)
constexpr int64_t  KCP_UPDATE_MS = 100;             // KCP UPDATE 间隔(毫秒)

constexpr int      IO_BLOCK_SIZE = 128;             // sendmsg/recvmsg msghdr.msg_iovlen 大小
constexpr int      IO_MSG_SIZE = 256;               // recvmmsg mmsghdr 大小

#ifdef _WIN32
    typedef SOCKET SOCKET;
#else
    typedef int SOCKET;
#endif

class KcpSess;

/// <summary>
/// RxSeg IO读取结构, 用于IO 读取数据
/// </summary>
struct RxSeg {
    static xq::tools::ObjectPool<RxSeg>* pool() {
        return xq::tools::ObjectPool<RxSeg>::Instance();
    }

    int len;            // 消息总长度
    KcpSess* sess;      // 消息来源
    socklen_t addrlen;  // 地址长度
    sockaddr addr;      // 地址
#ifndef WIN32
    uint8_t data[IO_BLOCK_SIZE][KCP_MTU];   // 数据块
#else
    uint8_t data[1][KCP_MTU];               // 数据块
#endif // !WIN32

    /// <summary>
    /// 构造函数
    /// </summary>
    explicit RxSeg()
        : len(KCP_MTU * IO_BLOCK_SIZE)
        , sess(nullptr)
        , addrlen(sizeof(addr))
        , addr({ 0,{0} }) {
        assert(data);
    }
}; // struct RxSeg;

typedef moodycamel::BlockingConcurrentQueue<RxSeg*> RxQueue;

/// <summary>
/// 错误类型
/// </summary>
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
        size_t pos = tmp.rfind(':');
        if (pos == std::string::npos)
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
        size_t pos = tmp.rfind(':');
        if (pos == std::string::npos) {
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

/// <summary>
/// 将sockaddr 转换成相应的字符串形式
/// </summary>
/// <param name="addr">sockaddr 结构</param>
/// <returns>sockaddr 字符串表示</returns>
std::string addr2str(const sockaddr *addr) {
    if (!addr) {
        return "";
    }

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

    default:
        return "";
    } // switch (addr_.sa_family);

    return rzt;
}

} // namespace net
} // namespace xq

#endif // __NET_HPP__
