#ifndef __XQ_NET_BASIC__
#define __XQ_NET_BASIC__


#include <atomic>
#include <regex>
#include <thread>
#include "xq/third/blockingconcurrentqueue.h"
#include "xq/net/net.in.h"


namespace xq {
namespace net {


struct Frame;
typedef moodycamel::BlockingConcurrentQueue<Frame*> FrameQueue;
constexpr int FRAME_QUE_SIZE = 1024 * 1024;

// IPv4 regex
constexpr char REG_IPV4[] = "^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$";
// IPv6 regex
constexpr char REG_IPV6[] = "^\\[(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\\]$";


/// @brief Check string of IP address family
/// @param ip IP to be cheked
/// @return AF_INET6/AF_INET on success or -1 on failure.
/// @remark
///     Complexity:  O(1)
///     System call:
inline int check_ip_family(const std::string& ip) {
    static const std::regex REG_V6(REG_IPV6);
    static const std::regex REG_V4(REG_IPV4);

    if (std::regex_match(ip, REG_V6)) {
        return AF_INET6;
    }

    if (std::regex_match(ip, REG_V4)) {
        return AF_INET;
    }

    return -1;
}


#ifdef _WIN32


/// @brief For lockfree mutex on windows
struct SpinLock {


    SpinLock() {}


    /// @brief Spin mutex lock
    /// @remark
    ///     Complexity:  O(n)
    ///     System call:
    void lock() {
        while (InterlockedCompareExchange(&m_, 1, 0) != 0) {
            _mm_pause();
        }
    }


    /// @brief Spin mutex unlock
    /// @remark
    ///     Complexity:  O(1)
    ///     System call:
    void unlock() {
        InterlockedExchange(&m_, 0);
    }


private:
    long m_ = 0;


    SpinLock(const SpinLock&) = delete;
    SpinLock(const SpinLock&&) = delete;
    SpinLock& operator =(const SpinLock&) = delete;
    SpinLock& operator =(const SpinLock&&) = delete;
}; // struct SpinLock;


#else


/// @brief For lockfree mutex on !windows
struct SpinLock {


    /// @brief Spin mutex lock
    /// @remark
    ///     Complexity:  O(n)
    ///     System call:
    void lock() {
        ASSERT(!pthread_spin_lock(&m_));
    }


    /// @brief Spin mutex unlock
    /// @remark
    ///     Complexity:  O(1)
    ///     System call:
    void unlock() {
        ASSERT(!pthread_spin_unlock(&m_));
    }


    SpinLock() {
        ASSERT(!pthread_spin_init(&m_, PTHREAD_PROCESS_PRIVATE));
    }


    ~SpinLock() {
        ASSERT(!pthread_spin_destroy(&m_));
    }


private:
    pthread_spinlock_t m_;


    SpinLock(const SpinLock&) = delete;
    SpinLock(const SpinLock&&) = delete;
    SpinLock& operator =(const SpinLock&) = delete;
    SpinLock& operator =(const SpinLock&&) = delete;
}; // struct SpinLock;


#endif // _WIN32


/// @brief Convert bytes to u64
/// @param p Bytes
/// @param v [out] The u64 to be converted
/// @return sizeof u64
inline int u64_decode(const uint8_t* p, uint64_t* v) {
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


/// @brief Convert bytes to s64
/// @param p Bytes
/// @param v [out] The s64 to be converted
/// @return sizeof s64
inline int s64_decode(const uint8_t* p, int64_t* v) {
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


/// @brief Convert bytes to u48
/// @param p Bytes
/// @param v [out] The u48 to be converted
/// @return 6
inline int u48_decode(const uint8_t* p, uint64_t* v) {
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


/// @brief Convert bytes to s48
/// @param p Bytes
/// @param v [out] The s48 to be converted
/// @return 6
inline int s48_decode(const uint8_t* p, int64_t* v) {
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


/// @brief Convert bytes to u32
/// @param p Bytes
/// @param v [out] The u32 to be converted
/// @return 4
inline int u32_decode(const uint8_t* p, uint32_t* v) {
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


/// @brief Convert bytes to s32
/// @param p Bytes
/// @param v [out] The s32 to be converted
/// @return 4
inline int s32_decode(const uint8_t* p, int32_t* v) {
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


/// @brief Convert bytes to u24
/// @param p Bytes
/// @param v [out] The u24 to be converted
/// @return 3
inline int u24_decode(const uint8_t* p, uint32_t* v) {
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


/// @brief Convert bytes to s24
/// @param p Bytes
/// @param v [out] The s24 to be converted
/// @return 3
inline int s24_decode(const uint8_t* p, int32_t* v) {
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


/// @brief Convert bytes to u16
/// @param p Bytes
/// @param v [out] The u16 to be converted
/// @return 2
inline int u16_decode(const uint8_t* p, uint16_t* v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 1) = *p;
    *tmp = *(p + 1);
#else
    memcpy(v, p, 2);
#endif
    return 2;
}


/// @brief Convert bytes to s16
/// @param p Bytes
/// @param v [out] The s16 to be converted
/// @return 2
inline int s16_decode(const uint8_t* p, int16_t* v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
    uint8_t* tmp = (uint8_t*)v;
    *(tmp + 1) = *p;
    *tmp = *(p + 1);
#else
    memcpy(v, p, 2);
#endif
    return 2;
}


/// @brief Convert bytes to u8
/// @param p Bytes
/// @param v [out] The u8 to be converted
/// @return sizeof(u8)
inline int u8_decode(const uint8_t* p, uint8_t* v) {
    *v = *p;
    return 1;
}


/// @brief Convert bytes to s8
/// @param p Bytes
/// @param v [out] The s8 to be converted
/// @return sizeof(s8)
inline int s8_decode(const uint8_t* p, int8_t* v) {
    *v = *p;
    return 1;
}


/// @brief Convert u64 into bytes
/// @param v The u64 to be converted
/// @param p [out] bytes
/// @return sizeof(u64)
inline int u64_encode(uint64_t v, uint8_t* p) {
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


/// @brief Convert s64 into bytes
/// @param v The s64 to be converted
/// @param p [out] bytes
/// @return sizeof(s64)
inline int s64_encode(int64_t v, uint8_t* p) {
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


/// @brief Convert u48 into bytes
/// @param v The u48 to be converted
/// @param p [out] bytes
/// @return 6
inline int u48_encode(uint64_t v, uint8_t* p) {
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


/// @brief Convert s48 into bytes
/// @param v The s48 to be converted
/// @param p [out] bytes
/// @return 6
inline int s48_encode(int64_t v, uint8_t* p) {
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


/// @brief Convert u32 into bytes
/// @param v The u32 to be converted
/// @param p [out] bytes
/// @return sizeof(u32)
inline int u32_encode(uint32_t v, uint8_t* p) {
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


/// @brief Convert s32 into bytes
/// @param v The s32 to be converted
/// @param p [out] bytes
/// @return sizeof(s32)
inline int s32_encode(int32_t v, uint8_t* p) {
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


/// @brief Convert u24 into bytes
/// @param v The u24 to be converted
/// @param p [out] bytes
/// @return 3
inline int u24_encode(uint32_t v, uint8_t* p) {
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


/// @brief Convert s24 into bytes
/// @param v The s24 to be converted
/// @param p [out] bytes
/// @return 3
inline int s24_encode(int32_t v, uint8_t* p) {
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


/// @brief Convert u16 into bytes
/// @param v The u16 to be converted
/// @param p [out] bytes
/// @return 2
inline int u16_encode(uint16_t v, uint8_t* p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 1);
    *(p + 1) = *tmp;
#else
    memcpy(p, tmp, 2);
#endif
    return 2;
}


/// @brief Convert s16 into bytes
/// @param v The s16 to be converted
/// @param p [out] bytes
/// @return 2
inline int s16_encode(int16_t v, uint8_t* p) {
    uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
    * p = *(tmp + 1);
    *(p + 1) = *tmp;
#else
    memcpy(p, tmp, 2);
#endif
    return 2;
}


/// @brief Convert u8 into bytes
/// @param v The u8 to be converted
/// @param p [out] bytes
/// @return sizeof u8
inline int u8_encode(uint8_t v, uint8_t* p) {
    *p = v;
    return 1;
}


/// @brief Convert s8 into bytes
/// @param v The s8 to be converted
/// @param p [out] bytes
/// @return sizeof s8
inline int s8_encode(int8_t v, uint8_t* p) {
    *p = v;
    return 1;
}


} // namespace net
} // namespace xq


#endif // __XQ_NET_BASIC__
