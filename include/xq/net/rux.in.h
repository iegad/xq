/* ********************************************************************************************************************************************
 * RUX: Reliable / Realtime user datagram protocol extension.
 * 
 * -----------------------------------------------------------------------------------------------------------------------------
 * @auth: iegad
 * @time: 2023-05-09
 * 
 * @update history
 * -----------------------------------------------------------------------------------------------------------------------------
 * @time                   | @note                                                                  |@coder
 * 
 * ******************************************************************************************************************************************* */


#ifndef __XQ_NET_RUX_IN__
#define __XQ_NET_RUX_IN__


#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
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
#endif

#include <stdint.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


/* ********************************************************************************************************************************************
 * 判定平台是否为大端序平台
 *
 * --------------------------
 * @auth: iegad
 * @time: 2023-05-10
 * ********************************************************************************************************************************************/
#ifndef X_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define X_BIG_ENDIAN 1
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
        #define X_BIG_ENDIAN 0
    #endif
#endif


/* ********************************************************************************************************************************************
 * 判定平台是否为强制对齐平台
 * 
 * --------------------------
 * @auth: iegad
 * @time: 2023-05-10
 * ********************************************************************************************************************************************/
#ifndef X_MUST_ALIGN
    #if defined(__i386__) || defined(__i386) || defined(_i386_)
        #define X_MUST_ALIGN 0
    #elif defined(_M_IX86) || defined(_X86_) || defined(__x86_64__)
        #define X_MUST_ALIGN 0
    #elif defined(__amd64) || defined(__amd64__) || defined(_AMD64_)
        #define X_MUST_ALIGN 0
    #else
        #define X_MUST_ALIGN 1
    #endif
#endif


#define ASSERT(expr) if (!(expr)){ fprintf(stderr, "%s:%d %s", __FILE__, __LINE__, #expr); abort(); }
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MIN3(a, b, c) (MIN(MIN(a, b), (c)))
#define MID(a, b, c) (MIN(MAX(a, b), c))


#if (_DEBUG == 1 || NDEBUG == 1 || __linux__ == 1)
#define DLOG(fmt, ...) (printf(fmt, __VA_ARGS__))
#else
#define DLOG(fmt, ...)
#endif // DEBUG



/* ********************************************************************************************************************************************
 * 平台统一接口
 *
 * --------------------------
 * @auth: iegad
 * @time: 2023-05-10
 * ********************************************************************************************************************************************/

/* system common macro */
#ifndef WIN32
    typedef int SOCKET;                         // socket type
    #define INVALID_SOCKET ((SOCKET)(~0))       // invalid socket
    #define errcode errno                       // get last io's error code for !win
#else
    #define close(fd) closesocket(fd)           // close socket
    #define errcode WSAGetLastError()           // get last io's error code for win
#endif // !WIN32


/* init netword environment, nothing todo on !win */
int rux_env_init();

/* release netword environment, nothing todo on !win */
int rux_env_release();

/* build a udp socket and bind with host/IP and svc/port */
SOCKET udp_bind(const char* host, const char* svc);

/* convert sockaddr to string */
int addr2str(const struct sockaddr_storage* addr, char* buf, size_t buflen);

/* convert string to sockaddr, addr->sa_family must be set: AF_INET or AF_INET6 */
int str2addr(const char *endpoint, struct sockaddr_storage *addr, socklen_t* addrlen);

/* make socket nonblocking */
int make_nonblocking(SOCKET sockfd);

/* get system clock */
int64_t sys_clock();

/* get count of cpu cores */
int sys_cpus();

int bin2hex(const uint8_t* data, size_t datalen, char* buf, size_t buflen);

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


#endif // !__XQ_NET_RUX_IN__
