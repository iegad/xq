#ifndef __XQ_NET_IN__
#define __XQ_NET_IN__


#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Iphlpapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <jemalloc/jemalloc.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <memory.h>
#include <emmintrin.h>
#include <sys/time.h>
#endif

#include <stdint.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#ifndef X_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define X_BIG_ENDIAN (1)
        #endif
    #endif
    #ifndef X_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define X_BIG_ENDIAN 1
        #endif
    #endif

    #ifndef X_BIG_ENDIAN
        #define X_BIG_ENDIAN (0)
    #endif
#endif


#ifndef X_MUST_ALIGN
    #if defined(__i386__) || defined(__i386) || defined(_i386_)
        #define X_MUST_ALIGN (0)
    #elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
        #define X_MUST_ALIGN (0)
    #elif defined(__amd64) || defined(__amd64__) || defined(_AMD64_)
        #define X_MUST_ALIGN (0)
    #else
        #define X_MUST_ALIGN (1)
    #endif
#endif


#define ASSERT(expr)    if (!(expr)){ fprintf(stderr, "%s:%d %s", __FILE__, __LINE__, #expr); abort(); }
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define MIN(a, b)       ((a) > (b) ? (b) : (a))
#define MIN3(a, b, c)   (MIN(MIN(a, b), (c)))
#define MAX3(a, b, c)   (MAX(MAX(a, b), (c)))
#define MID(a, b, c)    (MIN(MAX(a, b), c))


#if (_DEBUG || NDEBUG)
#define DLOG(...) (printf(__VA_ARGS__))
#else
#define DLOG(...) (printf(__VA_ARGS__))
#endif


#ifdef _WIN32
    #define close(fd) closesocket(fd)           // close socket
    #define errcode WSAGetLastError()           // get last io's error code for win
#else
    typedef int SOCKET;                         // socket type
    #define INVALID_SOCKET ((SOCKET)(~0))       // invalid socket
    #define errcode errno                       // get last io's error code for !win
#endif // !_WIN32


#define IPV4_HDR_SIZE       (20)                                            // IPv4 Header size
#define IPV6_HDR_SIZE       (40)                                            // IPv6 Header size
#define UDP_HDR_SIZE        (8)                                             // UDP Header size
#define ETH_FRM_SIZE        (1500)                                          // Ethernet payload size
#define UDP_MTU             (ETH_FRM_SIZE - UDP_HDR_SIZE - IPV6_HDR_SIZE)   // RUX Maximum Transmission Unit: 1452
#define ENDPOINT_STR_LEN    (INET6_ADDRSTRLEN + 7)                          // Endpoint' string max size


/// @brief Init network environment, nothing todo on !win
/// @return 0 on success or -1 on failure.
inline int rux_env_init() {
#ifdef _WIN32
    WSADATA wdata;
    return WSAStartup(0x0202, &wdata) < 0 || wdata.wVersion != 0x0202 ? -1 : 0;
#else
    return 0;
#endif // _WIN32
}


/// @brief Release network environment, nothing todo on !win
/// @return 0 on success or -1 on failure.
inline int rux_env_release() {
#ifdef _WIN32
    return WSACleanup();
#else
    return 0;
#endif // _WIN32
}


/// @brief Make udp socket then bind [host:svc];
/// @param host host/ip
/// @param svc  svc/port
/// @return sockfd on success or INVALID_SOCKET on failure.
/// @remark:
///     Complexity:  O(n)
///     System call: bind, socket
SOCKET udp_bind(const char* host, const char* svc);


/// @brief Convert sockaddr to string
/// @param addr     the sockaddr to be converted
/// @param buf      [out] string buf
/// @param buflen   [out] string buf's length
/// @return 0 on success or -1 on failure.
/// @remark:
///     Complexity:  O(1)
///     System call: inet_ntop
int addr2str(const struct sockaddr_storage* addr, char* buf, size_t buflen);


/// @brief Convert string to sockaddr
/// @param endpoint the endpoint's string to be converted
/// @param addr     [out] sockaddr's pointer
/// @param addrlen  [out] sockaddrlen's pointer
/// @return 0 on success or -1 on failure.
/// @note 'addr.ss_family' must be set AF_INET / AF_INET6
/// @remark:
///     Complexity:  O(1)
///     System call: inet_pton
int str2addr(const char *endpoint, struct sockaddr_storage *addr, socklen_t* addrlen);


/// @brief Set sockfd nonblocking
/// @param sockfd the sockfd to be set nonblocking
/// @return 0 on success or -1 on failure.
/// @remark:
///     Complexity:  O(1)
///     System call: fcntl/ioctlsocket
inline int make_nonblocking(SOCKET sockfd) {
#ifdef _WIN32
    static u_long ON = 1;
    return ioctlsocket(sockfd, FIONBIO, &ON);
#else
    int opts = fcntl(sockfd, F_GETFL);
    return opts < 0 ? -1 : fcntl(sockfd, F_SETFL, opts | O_NONBLOCK);
#endif
}


/// @brief Get clock(us) of since the system started.
/// @note  It's not unix timetamp.
/// @remark:
///     Complexity:  O(1)
///     System call: clock_gettime / QueryPerformanceFrequency, QueryPerformanceCounter
inline int64_t sys_clock() {
#ifdef _WIN32
    static int64_t FREQ = 0;
    LARGE_INTEGER l;
    if (FREQ == 0) {
        LARGE_INTEGER f;
        ASSERT(QueryPerformanceFrequency(&f));
        FREQ = f.QuadPart / 1000000;
    }

    ASSERT(QueryPerformanceCounter(&l));
    return l.QuadPart / FREQ;
#else
    struct timespec t;
    ASSERT(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec * 1000000 + t.tv_nsec / 1000;
#endif // !_WIN32
}


/// @brief  Get unix timestamp(us)
/// @return Current unix timestamp
/// @remark:
///     Complexity:  O(1)
///     System call: gettimeofday / GetSystemTimeAsFileTime
inline int64_t sys_time() {
#ifdef _WIN32
#define EPOCHFILETIME (116444736000000000)
    FILETIME ft;
    LARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return (li.QuadPart - EPOCHFILETIME) / 10;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
#endif // _WIN32
}


/// @brief Get number of cpu's cores.
/// @return number of cpu's cores.
/// @remark:
///     Complexity:  O(1)
///     System call: sysconf / GetSystemInfo
inline int sys_cpus() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
#else
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif // _WIN32
}


/// @brief Get local ip address
/// @param addrs    [out] sockaddr_storage array
/// @param n        number of sockaddr_storage array
/// @return         number of local ip address on success or -1 on failure.
/// @remark:
///     Complexity:  O(n)
///     System call: gethostname, getaddrinfo
int sys_ips(struct sockaddr_storage *addrs, size_t n);


/// @brief Convert binary data to hex string.
/// @param data     the binary data to be converted
/// @param datalen  binary data's length
/// @param buf      [out] string buffer
/// @param buflen   string buffer's length
/// @return The length after converting to a hexadecimal string or -1 on failure.
/// @remark:
///     Complexity:  O(n)
///     System call: 
int bin2hex(const uint8_t* data, size_t datalen, char* buf, size_t buflen);


/// @brief Convert hex string to binary data.
/// @param data     the hex string to be converted
/// @param datalen  hex string's length
/// @param buf      [out] binary buffer
/// @param buflen   binary buffer's length
/// @return The length after conversion to binary data or -1 on failure.
/// @remark:
///     Complexity:  O(n)
///     System call: 
int hex2bin(const char* data, size_t datalen, uint8_t* buf, size_t buflen);


inline int u64_decode(const uint8_t * p, uint64_t * v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 7) = *p;
    *(tmp + 6) = *(p + 1);
    *(tmp + 5) = *(p + 2);
    *(tmp + 4) = *(p + 3);
    *(tmp + 3) = *(p + 4);
    *(tmp + 2) = *(p + 5);
    *(tmp + 1) = *(p + 6);
    *tmp = *(p + 7);
#else
    memcpy(v, p, 8);
#endif
    return 8;
}


inline int u48_decode(const uint8_t * p, uint64_t * v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 7) = *p;
    *(tmp + 6) = *(p + 1);
    *(tmp + 5) = *(p + 2);
    *(tmp + 4) = *(p + 3);
    *(tmp + 3) = *(p + 4);
    *(tmp + 2) = *(p + 5);
#else
    memcpy(v, p, 6);
#endif
    return 6;
}


inline int u32_decode(const uint8_t * p, uint32_t * v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 3) = *p;
    *(tmp + 2) = *(p + 1);
    *(tmp + 1) = *(p + 2);
    *tmp = *(p + 3);
#else
    memcpy(v, p, 4);
#endif
    return 4;
}


inline int u24_decode(const uint8_t * p, uint32_t * v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 3) = *p;
    *(tmp + 2) = *(p + 1);
    *(tmp + 1) = *(p + 2);
#else
    memcpy(v, p, 3);
#endif
    return 3;
}


inline int u16_decode(const uint8_t * p, uint16_t * v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 1) = *p;
    *tmp = *(p + 1);
#else
    memcpy(v, p, 2);
#endif
    return 2;
}


inline int u8_decode(const uint8_t * p, uint8_t * v) {
    *v = *p;
    return 1;
}


inline int u64_encode(uint64_t v, uint8_t * p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 7);
    *(p + 1) = *(tmp + 6);
    *(p + 2) = *(tmp + 5);
    *(p + 3) = *(tmp + 4);
    *(p + 4) = *(tmp + 3);
    *(p + 5) = *(tmp + 2);
    *(p + 6) = *(tmp + 1);
    *(p + 7) = *tmp;
#else
    memcpy(p, tmp, 8);
#endif
    return 8;
}


inline int
u48_encode(uint64_t v, uint8_t * p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 7);
    *(p + 1) = *(tmp + 6);
    *(p + 2) = *(tmp + 5);
    *(p + 3) = *(tmp + 4);
    *(p + 4) = *(tmp + 3);
    *(p + 5) = *(tmp + 2);
#else
    memcpy(p, tmp, 6);
#endif
    return 6;
}


inline int u32_encode(uint32_t v, uint8_t * p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 3);
    *(p + 1) = *(tmp + 2);
    *(p + 2) = *(tmp + 1);
    *(p + 3) = *tmp;
#else
    memcpy(p, tmp, 4);
#endif
    return 4;
}


inline int u24_encode(uint32_t v, uint8_t * p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 3);
    *(p + 1) = *(tmp + 2);
    *(p + 2) = *(tmp + 1);
#else
    memcpy(p, tmp, 3);
#endif
    return 3;
}


inline int u16_encode(uint16_t v, uint8_t * p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 1);
    *(p + 1) = *tmp;
#else
    memcpy(p, tmp, 2);
#endif
    return 2;
}


inline int u8_encode(uint8_t v, uint8_t * p) {
    *p = v;
    return 1;
}


#ifdef __cplusplus
}
#endif // __cplusplus


#endif // !__XQ_NET_IN__
