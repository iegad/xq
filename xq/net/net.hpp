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
#include <regex>
#include <thread>
#include <vector>


namespace xq {
namespace net {

// ------------------------------------------------------------------------ 常量 ------------------------------------------------------------------------


constexpr size_t   KCP_WND           = 512;        // KCP 默认读/写窗口
constexpr size_t   KCP_MTU           = 1418;       // KCP 最大传输单元
constexpr size_t   KCP_MAX_DATA_SIZE = 1418 * 128; // KCP 单包最大字节
constexpr size_t   KCP_HEAD_SIZE     = 24;         // KCP 消息头长度
constexpr uint32_t KCP_TIMEOUT       = 60000;      // KCP 默认超时(毫秒)
constexpr int64_t  KCP_UPDATE_MS     = 10;         // KCP UPDATE 间隔(毫秒)

constexpr int      IO_BLOCK_SIZE     = 128;        // sendmsg/recvmsg msghdr.msg_iovlen 大小
constexpr int      IO_MSG_SIZE       = 256;        // recvmmsg mmsghdr 大小
constexpr int      IO_TIMEOUT        = 5000;       // IO 读超时 5000毫秒

const std::regex REG_IPv4("^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$");
const std::regex REG_IPv6("(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))");


// ------------------------------------------------------------------------ 类型与符号 ------------------------------------------------------------------------


#ifdef WIN32
typedef SOCKET SOCKET;
#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#endif


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
SOCKET udp_bind(const std::string &local, sockaddr* addr = nullptr, socklen_t* addrlen = nullptr) {
    constexpr int ON = 1;

    assert(local.size() > 0 && "local is invalid");
    SOCKET fd = INVALID_SOCKET;

    // 本端地址用于udp 套接字绑定
    size_t pos = local.rfind(':');
    if (pos == std::string::npos) {
        return INVALID_SOCKET;
    }

    std::string ip = local.substr(0, pos);
    if (ip.empty()) {
        ip = "0.0.0.0";
    }

    std::string port = local.substr(pos + 1);

    addrinfo hints;
    addrinfo *result = nullptr, *rp = nullptr;

    ::memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result)) {
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

    default:
        return "";
    } // switch (addr_.sa_family);

    return rzt;
}


/// @brief 将字符串 endpoint 转换成sockaddr
/// @param str     有效的地址字符串
/// @param addr    OUT sockaddr
/// @param addrlen OUT sockaddr length
/// @return 成功转换返回true, 否则返回false
bool str2addr(const std::string& str, sockaddr *addr, socklen_t *addrlen) {
    if (!addr || !addrlen) {
        return false;
    }

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

    if (std::regex_match(ip, REG_IPv4)) {
        sockaddr_in* a4 = (sockaddr_in*)addr;
        a4->sin_family = AF_INET;
        if (::inet_pton(AF_INET, ip.c_str(), &a4->sin_addr) != 1) {
            return false;
        }

        a4->sin_port = ntohs((uint16_t)port);
        *addrlen = sizeof(sockaddr_in);
        return true;
    }
    else if (std::regex_match(ip, REG_IPv6)) {
        sockaddr_in6 *a6 = (sockaddr_in6*)addr;
        a6->sin6_family = AF_INET6;
        if (::inet_pton(AF_INET6, ip.c_str(), &a6->sin6_addr) != 1) {
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


#endif // __NET_HPP__
