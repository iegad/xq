#ifndef __TOOLS_HPP__
#define __TOOLS_HPP__

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif // !_WIN32

#include <stdint.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <iostream>

#include "xq/third/concurrentqueue.h"
#include "xq/net/net.hpp"

namespace xq {
namespace tools {


template<typename T> 
T MAX(const T &a, const T &b) {
    return a > b ? a : b;
}


template<typename T>
T MIN(const T& a, const T& b) {
    return a < b ? a : b;
}


template<typename T>
T MID(const T& a, const T& b, const T& c) {
    return MIN(MAX(a, b), c);
}

// ---------------------------------------------------------------------------- 时间 ----------------------------------------------------------------------------

/* --------------------------------------------------------------------- */
/// @brief
/// @return
///
inline int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


/* --------------------------------------------------------------------- */
/// @brief
/// @return
///
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


/* --------------------------------------------------------------------- */
/// @brief
/// @return
///
inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


/* --------------------------------------------------------------------- */
/// @brief
/// @return
///
inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------- 字节序 ----------------------------------------------------------------------------

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
#define X_BIG_ENDIAN  0
#endif
#endif

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


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint16_t __rvs_u16(uint16_t v) {
    return (v << 8) | (v >> 8);
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint32_t __rvs_u32(uint32_t v) {
    return (((uint32_t)(__rvs_u16(v)) << 16)) | (__rvs_u16(v >> 16));
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint64_t __rvs_u64(uint64_t v) {
    return (((uint64_t)(__rvs_u32(v)) << 32)) | (__rvs_u32(v >> 32));
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @return
///
inline bool is_le() {
    return !X_BIG_ENDIAN;
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @return
///
inline bool is_be() {
    return X_BIG_ENDIAN;
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint16_t to_le_u16(uint16_t v) {
#if X_BIG_ENDIAN
    return __rvs_u16(v);
#else
    return v;
#endif // X_BIG_ENDIAN

}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint16_t to_be_u16(uint16_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u16(v);
#endif // X_BIG_ENDIAN
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint32_t to_le_u32(uint32_t v) {
#if X_BIG_ENDIAN
    return __rvs_u32(v);
#else
    return v;
#endif // X_BIG_ENDIAN
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint32_t to_be_u32(uint32_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u32(v);
#endif // X_BIG_ENDIAN
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint64_t to_le_u64(uint64_t v) {
#if X_BIG_ENDIAN
    return __rvs_u64(v);
#else
    return v;
#endif // X_BIG_ENDIAN
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
inline uint64_t to_be_u64(uint64_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u64(v);
#endif // X_BIG_ENDIAN
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
std::string bin2hex(const uint8_t* data, size_t data_len) {
    assert(data);

    if (!data_len)
        return "";

    std::string res(data_len * 2, 0);
    uint8_t tmp;

    for (size_t i = 0; i < data_len; i++) {
        tmp = data[i];
        for (size_t j = 0; j < 2; j++) {
            uint8_t c = (tmp & 0x0f);
            if (c < 10)
                c += '0';
            else
                c += ('A' - 10);

            res[2 * i + 1 - j] = c;
            tmp >>= 4;
        }
    }
    return res;
}


/* --------------------------------------------------------------------- */
/// @brief
///
/// @param v
///
/// @return
///
int hex2bin(const std::string& hex, uint8_t *data, size_t *data_len) {
    size_t nhex = hex.empty();
    size_t n = nhex / 2;

    if (!nhex || nhex % 2 != 0 || !data || !data_len || *data_len < n)
        return -1;

    for (size_t i = 0; i < n; i++) {
        uint8_t tmp = 0;

        for (size_t j = 0; j < 2; j++) {
            char c = hex[2 * i + j];
            if (c >= '0' && c <= '9')
                tmp = (tmp << 4) + (c - '0');
            else if (c >= 'a' && c <= 'f')
                tmp = (tmp << 4) + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                tmp = (tmp << 4) + (c - 'A' + 10);
            else return -1;
        }
        data[i] = tmp;
    }

    *data_len = n;
    return (int)n;
}

// ---------------------------------------------------------------------------- spin lock  ----------------------------------------------------------------------------

class SpinLock {
public:
    void lock() {
        while (m_.test_and_set(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
    }

    void unlock() {
        m_.clear(std::memory_order_relaxed);
    }

private:
    std::atomic_flag m_;
}; // class SpinLock

// ---------------------------------------------------------------------------- safe btree set ----------------------------------------------------------------------------
template <typename T>
class Set {
public:
    explicit Set() = default;
    ~Set() = default;

    Set(const Set&) = delete;
    Set& operator=(const Set&) = delete;

    auto insert(const T& v) {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.insert(v);
    }

    auto erase(const T& v) {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.erase(v);
    }

    auto count(const T& v) {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.count(v);
    }

    auto size() {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.size();
    }

    auto begin() {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.begin();
    }

    auto end() {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.end();
    }

    size_t as_vec(std::vector<T> &vec) {
        size_t i = 0;
        std::lock_guard<std::mutex> lk(mtx_);

        if (!m_.empty()) {
            for (auto& v : m_) {
                vec[i++] = v;
            }
        }

        return i;
    }

    auto find(const T& v) {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.find(v);
    }

private:
    std::mutex mtx_;
    std::unordered_set<T> m_;
}; // class Set;

// ---------------------------------------------------------------------------- safe map ----------------------------------------------------------------------------

template <typename TKey, typename TVal, typename TLock = std::mutex>
class Map {
public:
    Map() = default;
    ~Map() = default;
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    size_t get_all_vals(TVal* vals, size_t n) {
        size_t i = 0;
        std::lock_guard<TLock> lk(mtx_);
        assert(n >= m_.size());

        for (auto &itr: m_) {
            vals[i++] = itr.second;
        }
        return i;
    }

    auto insert(const TKey& k, const TVal& v) {
        std::lock_guard<TLock> lk(mtx_);
        return m_.insert(std::make_pair(k, v));
    }

    bool get(const TKey& k, TVal &v) {
        std::lock_guard<TLock> lk(mtx_);
        auto itr = m_.find(k);
        if (itr == m_.end()) {
            return false;
        }

        v = itr->second;
        return true;
    }

    void clear() {
        std::lock_guard<TLock> lk(mtx_);
        m_.clear();
    }

    auto erase(const TKey *keys, size_t n) {
        std::lock_guard<TLock> lk(mtx_);
        for (size_t i = 0; i < n; i++) {
            m_.erase(keys[i]);
        }
    }

    auto size() {
        std::lock_guard<TLock> lk(mtx_);
        return m_.size();
    }

private:
    TLock mtx_;
    std::unordered_map<TKey, TVal> m_;
};


// ---------------------------------------------------------------------------- BTreeTimer  ----------------------------------------------------------------------------

typedef void(*TimerHandler)(void* arg);

class BTreeTimer final {
public:
    // ------------------------------------------------------------------ BEG Timer  ------------------------------------------------------------------
    struct Timer {
        uint64_t id;
        int64_t time_ms;
        TimerHandler handler;
        void* arg;

        void action() {
            if (handler) {
                handler(arg);
            }
        }
    };
    // ------------------------------------------------------------------ END Timer  ------------------------------------------------------------------


    typedef std::shared_ptr<BTreeTimer> Ptr;
    typedef std::unordered_map<uint64_t, Timer*> Slot;


    static Ptr create() {
        return Ptr(new BTreeTimer);
    }


    ~BTreeTimer() {
        for (auto& slot_itr : slotm_) {
            Slot* slot = slot_itr.second;
            for (auto& timer : *slot) {
                delete timer.second;
            }
            delete slot;
        }
    }


    void start(int interval = 15) {
        running_ = true;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            int64_t now_ms = xq::tools::now_ms();
            mtx_.lock();
            for (auto slot_itr = slotm_.begin(); slot_itr != slotm_.end();) {
                if (slot_itr->first > now_ms) {
                    break;
                }

                Slot *slot = slot_itr->second;
                for (auto& timer_itr : *slot) {
                    Timer* timer = timer_itr.second;
                    timer->action();
                    delete timer;
                }

                delete slot;
                slotm_.erase(slot_itr++);
            }
            mtx_.unlock();
        }
    }


    Timer* create_timer_at(int64_t expir_ms, TimerHandler handler, void* arg) {
        if (expir_ms <= xq::tools::now_ms()) {
            handler(arg);
            return nullptr;
        }

        Timer* timer = new Timer;
        timer->id = id_gen_++;
        timer->time_ms = expir_ms;
        timer->handler = handler;
        timer->arg = arg;

        mtx_.lock();
        auto itr = slotm_.find(expir_ms);
        if (itr != slotm_.end()) {
            itr->second->insert(std::make_pair(timer->id, timer));
        }
        else {
            Slot* slot = new Slot;
            slot->insert(std::make_pair(timer->id, timer));
            slotm_.insert(std::make_pair(expir_ms, slot));
        }
        mtx_.unlock();

        return timer;
    }


    Timer* create_timer_after(int64_t delay_ms, TimerHandler handler, void* arg) {
        if (delay_ms <= 0) {
            return nullptr;
        }

        return create_timer_at(xq::tools::now_ms() + delay_ms, handler, arg);
    }


    void remove_timer(Timer* timer) {
        mtx_.lock();
        auto slot_itr = slotm_.find(timer->time_ms);

        if (slot_itr != slotm_.end()) {
            Slot* slot = slot_itr->second;
            auto itr_timer = slot->find(timer->id);
            if (itr_timer != slot->end()) {
                delete itr_timer->second;
                slot->erase(itr_timer);
            }

            if (slot->empty()) {
                delete slot;
                slotm_.erase(slot_itr);
            }
        }
        mtx_.unlock();
    }


    void stop() {
        mtx_.lock();
        running_ = false;
        slotm_.clear();
        mtx_.unlock();
    }


private:
    BTreeTimer()
        : running_(false)
    {}


    bool running_;
    std::atomic<uint64_t> id_gen_;
    std::mutex mtx_;
    std::map<int64_t, Slot*> slotm_;


    BTreeTimer(const BTreeTimer&) = delete;
    BTreeTimer(const BTreeTimer&&) = delete;
    BTreeTimer& operator=(const BTreeTimer&) = delete;
    BTreeTimer& operator=(const BTreeTimer&&) = delete;
}; // class TimerScheduler;

} // namespace tools
} // namespace xq




#endif // __TOOLS_HPP__
