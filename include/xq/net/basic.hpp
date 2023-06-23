#ifndef __XQ_NET_BASIC__
#define __XQ_NET_BASIC__


#include <atomic>
#include <regex>
#include <thread>
#include <list>
#include <map>
#include "xq/net/net.in.h"


namespace xq {
namespace net {


// IPv4 regex
constexpr char REG_IPV4[] = "^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$";
// IPv6 regex
constexpr char REG_IPV6[] = "^\\[(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\\]$";


inline int check_ip_type(const std::string& ip) {
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
struct SpinLock {
    void lock() {
        while (InterlockedCompareExchange(&m_, 1, 0) != 0) {
            _mm_pause();
        }
    }

    inline void unlock() {
        InterlockedExchange(&m_, 0);
    }


    SpinLock() : m_(0) {}

private:
    long m_;
};

#define LOCK  lkr_.lock();DLOG("%s LOCKED\n", __func__);
#define UNLOCK lkr_.unlock(); DLOG("%s UNLOCKED\n", __func__);

#else
struct SpinLock {
    inline void lock() {
        ASSERT(!pthread_spin_lock(&m_));
    }

    inline void unlock() {
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
};
#endif // _WIN32


} // namespace net
} // namespace xq


#endif // __XQ_NET_BASIC__
