#include "net/net.hpp"
#include "tools/tools.hpp"

// -------------------------------------------------------------------------------------- 公共函数 --------------------------------------------------------------------------------------

SOCKET
xq::net::udp_socket(const char* local, const char* remote, sockaddr *addr, socklen_t *addrlen) {
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
