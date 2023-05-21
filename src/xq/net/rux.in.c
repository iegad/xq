#include "xq/net/rux.in.h"
#include <string.h>

int 
rux_env_init() {
#ifdef WIN32
    WSADATA wdata;
    return WSAStartup(0x0202, &wdata) < 0 || wdata.wVersion != 0x0202 ? -1 : 0;
#else
    return 0;
#endif // WIN32

}


int 
rux_env_release() {
#ifdef WIN32
    return WSACleanup();
#else
    return 0;
#endif // WIN32
}


SOCKET
udp_bind(const char* host, const char* svc) {
    static const int ON = 1;

    if (!host || !svc) {
        return INVALID_SOCKET;
    }

    SOCKET fd = INVALID_SOCKET;
    struct addrinfo hints;
    struct addrinfo *result = NULL, *rp = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(host, svc, &hints, &result)) {
        return INVALID_SOCKET;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) {
            continue;
        }

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
            close(fd);
            return INVALID_SOCKET;
        }

#ifndef WIN32
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
            close(fd);
            return INVALID_SOCKET;
        }
#endif // !WIN32

        if (!bind(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
            break;
        }

        close(fd);
        fd = INVALID_SOCKET;
    }

    if (!rp) {
        fd = INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return fd;
}


int
addr2str(const struct sockaddr_storage* addr, char* buf, size_t nbuf) {
    ASSERT(addr && buf && nbuf >= 48 && (addr->ss_family == AF_INET || addr->ss_family == AF_INET6));

    switch (addr->ss_family) {
    case AF_INET: {
        struct sockaddr_in* ra = (struct sockaddr_in*)addr;
        if (inet_ntop(AF_INET, &ra->sin_addr, buf, nbuf)) {
            char* tmpbuf = buf + strlen(buf);
            snprintf(tmpbuf, nbuf, ":%d", ntohs(ra->sin_port));
            return 0;
        }
    } break;

    case AF_INET6: {
        struct sockaddr_in6* ra = (struct sockaddr_in6*)addr;
        if (inet_ntop(AF_INET6, &ra->sin6_addr, buf, nbuf)) {
            buf[0] = '[';
            int len = strlen(buf);
            buf[len] = ']';
            char* tmpbuf = buf + len + 1;
            snprintf(tmpbuf, nbuf, ":%d", ntohs(ra->sin6_port));
            return 0;
        }
    } break;

    default:
        break;
    } // switch (addr_.sa_family);

    return -1;
}


int
str2addr(const char* endpoint, struct sockaddr_storage* addr, socklen_t* addrlen) {
    ASSERT(endpoint && addr && addrlen && (addr->ss_family == AF_INET || addr->ss_family == AF_INET6));

    char ip[INET6_ADDRSTRLEN] = {0};
    char *svc = strrchr(endpoint, ':');
    int pos = svc++ - endpoint;

    if (pos <= 0) {
        return -1;
    }

    int port = 0;
    if (!sscanf(svc, "%d", &port) || port > 65535) {
        return -1;
    }

    memcpy(ip, endpoint, pos);

    switch (addr->ss_family) {

    case AF_INET: {
        struct sockaddr_in* p = (struct sockaddr_in*)addr;
        if (inet_pton(AF_INET, ip, &p->sin_addr) != 1) {
            return -1;
        }

        p->sin_port = ntohs((uint16_t)port);
        *addrlen = sizeof(struct sockaddr_in);
    } break;

    case AF_INET6: {
        struct sockaddr_in6* p = (struct sockaddr_in6*)addr;
        ip[pos - 1] = 0;
        if (inet_pton(AF_INET6, ip + 1, &p->sin6_addr) != 1) {
            return -1;
        }

        p->sin6_port = ntohs((uint16_t)port);
        *addrlen = sizeof(struct sockaddr_in6);
    } break;
    }

    return 0;
}


int 
make_nonblocking(SOCKET sockfd) {
#ifndef WIN32
    int opts = fcntl(sockfd, F_GETFL);
    return opts < 0 ? -1 : fcntl(sockfd, F_SETFL, opts | O_NONBLOCK);
#else
    static u_long ON = 1;
    return ioctlsocket(sockfd, FIONBIO, &ON);
#endif // !WIN32
}


int64_t 
sys_clock() {
#ifdef WIN32
    LARGE_INTEGER l;
    ASSERT(QueryPerformanceCounter(&l));
    return l.QuadPart;
#else
    struct timespec t;
    ASSERT(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec * 1000000 + t.tv_nsec / 1000;
#endif // !WIN32
}


int
sys_cpus() {
#ifdef WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif // !WIN32
}


int 
bin2hex(const uint8_t* data, size_t datalen, char* buf, size_t buflen) {
    ASSERT(data && buf && datalen > 0 && buflen >= datalen << 1);

    uint8_t tmp;

    for (size_t i = 0; i < datalen; i++) {
        tmp = data[i];
        for (size_t j = 0; j < 2; j++) {
            uint8_t c = (tmp & 0x0f);
            if (c < 10)
                c += '0';
            else
                c += ('A' - 10);

            buf[2 * i + 1 - j] = c;
            tmp >>= 4;
        }
    }
    return datalen << 1;
}
