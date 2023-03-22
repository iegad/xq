#ifndef __XQ_NET__
#define __XQ_NET__


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
#include <regex>
#include <thread>
#include <vector>


namespace xq {
namespace net {


// ------------------------------------------------------------------------ 常量 ------------------------------------------------------------------------

constexpr int IPV4_HEAD_SIZE = 20;
constexpr int IPV6_HEAD_SIZE = 40;
constexpr int UDP_HEAD_SIZE = 8;
constexpr int TCP_HEAD_SIZE = 20;
constexpr int ETH_FRAME_SIZE = 1500;
constexpr int UDP_RBUF_SIZE = ETH_FRAME_SIZE - IPV6_HEAD_SIZE - UDP_HEAD_SIZE; // 读缓冲区大小
constexpr int IO_MSG_SIZE = 256;                             // recvmmsg mmsghdr 大小
constexpr int IO_TIMEOUT = 5000;                            // IO 读超时 5000毫秒

constexpr char REG_IPV4[] = "^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$";
constexpr char REG_IPV6[] = "^\\[(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\\]$";

// ------------------------------------------------------------------------ 类型与符号 ------------------------------------------------------------------------


#ifdef WIN32
typedef SOCKET SOCKET;
#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#endif


enum class ErrType {
    IO_RECV,     // IO 读失败
    IO_SEND,     // IO 写失败

    KCP_HEAD,    // KCP 消息头错误
    KCP_INPUT,   // KCP input 失败
    KCP_RECV,

    KL_INVALID_CONV, // KCP conv 无效

    KC_HOST,
};


constexpr int EK_INVALID      = -10000;
constexpr int EK_CONV         = -10001;
constexpr int EK_MAX_CONN     = -10002;
constexpr int EK_UNKNOWN_HOST = -10003; // 未知的服务端


// ------------------------------------------------------------------------ State ------------------------------------------------------------------------


/// @brief Kcp节点状态
enum class State {
    Stopped,  // 停止
    Stopping, // 停止中
    Running   // 运行
};


// ------------------------------------------------------------------------ 函数 ------------------------------------------------------------------------


/// @brief 关闭 sockfd
/// @param sockfd 需要关闭的sockfd
/// @return 成功返回 0, 否则返回 !0. 通过 error() 查询错误码.
inline int close(SOCKET sockfd) {
    if (sockfd == INVALID_SOCKET) {
        return 0;
    }

#ifndef _WIN32
    return ::close(sockfd);
#else
    return ::closesocket(sockfd);
#endif
}

/// @brief 获取IO错误码
/// @return 相应的错误码
inline int error() {
#ifndef _WIN32
    return errno;
#else
    return WSAGetLastError();
#endif
}


/// @brief 创建udp sockfd 并绑定local地址
/// @param local    需要绑定的地址
/// @param addr     OUT 参数, 用于接收sockaddr
/// @param addrlen  OUT 参数, 用于接收sockaddr length
/// @return 成功返回创建好的 UDP sockfd, 则返回 INVALID_SOCKET
/// @note   只有当addr和addrlen两个参数同时传递时, 才能获取绑定的sockaddr.
/// @exception 当local为无效地址时.
SOCKET udp_bind(const char *ip, const char *port, sockaddr* addr = nullptr, socklen_t* addrlen = nullptr) {
    constexpr int ON = 1;

    assert(ip && port);
    SOCKET fd = INVALID_SOCKET;

    addrinfo hints;
    addrinfo *result = nullptr, *rp = nullptr;

    ::memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (::getaddrinfo(ip, port, &hints, &result)) {
        return INVALID_SOCKET;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) {
            continue;
        }

        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
            close(fd);
            return INVALID_SOCKET;
        }

#ifndef WIN32
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
            close(fd);
            return INVALID_SOCKET;
        }
#endif

        if (!::bind(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
            if (addr && addrlen) {
                *addrlen = rp->ai_addrlen;
                ::memcpy(addr, rp->ai_addr, *addrlen);
            }
            break;
        }

        close(fd);
        fd = INVALID_SOCKET;
    }

    assert(rp);

    ::freeaddrinfo(result);

    return fd;
}


/// @brief 将sockaddr 转换成字符串形式
/// @param addr 需要获取的sockaddr
/// @return 成功返回 sockaddr字符串表达示, 否则返回空串.
std::string addr2str(const sockaddr *addr) {
    assert(addr && "addr is invalid");

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

    default: break;
    } // switch (addr_.sa_family);

    return rzt;
}


int addr2str(const sockaddr* addr, char* buf, size_t nbuf = 48) {
    assert(addr && "addr is invalid");
    assert(buf && nbuf >= 48 && "buf is invalid");

    ::memset(buf, 0, nbuf);

    switch (addr->sa_family) {

    case AF_INET: {
        sockaddr_in* ra = (sockaddr_in*)addr;
        assert(::inet_ntop(AF_INET, &ra->sin_addr, buf, nbuf) && "inet_ntop failed");
        char* tmpbuf = buf + strlen(buf);
        snprintf(tmpbuf, nbuf, ":%d", ntohs(ra->sin_port));
    } break;

    case AF_INET6: {
        sockaddr_in6* ra = (sockaddr_in6*)addr;
        assert(::inet_ntop(AF_INET6, &ra->sin6_addr, buf + 1, nbuf) && "inet_ntop failed");
        buf[0] = '[';
        int len = strlen(buf);
        buf[len] = ']';
        char* tmpbuf = buf + len + 1;
        snprintf(tmpbuf, nbuf, ":%d", ntohs(ra->sin6_port));
    } break;

    default:
        return -1;
    } // switch (addr_.sa_family);

    return strlen(buf);
}


/// @brief 将字符串 endpoint 转换成sockaddr
/// @param str     有效的地址字符串
/// @param addr    OUT sockaddr
/// @param addrlen OUT sockaddr length
/// @return 成功转换返回true, 否则返回false
bool str2addr(const std::string& str, sockaddr *addr, socklen_t *addrlen) {
    static std::regex r6(REG_IPV6);
    static std::regex r4(REG_IPV4);

    if (!addr || !addrlen) {
        return false;
    }

    ::memset(addr, 0, sizeof(*addr));

    bool has_port = false;
    int32_t port = 0;
    std::string ip = str, sport;
    size_t pos = str.rfind(':');
    if (pos != std::string::npos) {
        ip = str.substr(0, pos);
        sport = str.substr(pos + 1);
        has_port = true;
    }

    if (has_port) {
        port = std::stol(sport);
        if (port > 65535 || port < 0) {
            return false;
        }
    }

    if (std::regex_match(ip, r4)) {
        sockaddr_in* a4 = (sockaddr_in*)addr;
        a4->sin_family = AF_INET;
        if (::inet_pton(AF_INET, ip.c_str(), &a4->sin_addr) != 1) {
            return false;
        }

        a4->sin_port = ntohs((uint16_t)port);
        *addrlen = sizeof(sockaddr_in);
        return true;
    }
    else if (std::regex_match(ip, r6)) {
        sockaddr_in6 *a6 = (sockaddr_in6*)addr;
        a6->sin6_family = AF_INET6;
        if (::inet_pton(AF_INET6, std::string(ip.c_str() + 1, ip.size() - 2).c_str(), &a6->sin6_addr) != 1) {
            return false;
        }

        a6->sin6_port = ntohs(port);
        *addrlen = sizeof(sockaddr_in6);
        return true;
    }

    return false;
}


} // namespace net
} // namespace xq


#endif // __XQ_NET__
