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


} // namespace net
} // namespace xq


#endif // __XQ_NET_BASIC__
