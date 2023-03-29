#ifndef __XQ_TOOLS_HPP__
#define __XQ_TOOLS_HPP__


#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif // !_WIN32

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <vector>


#define ASSERT(expr) if (!(expr)){std::printf("%s:%d %s", __FILE__, __LINE__, #expr); std::terminate();}


namespace xq {
namespace tools {


/* -------------------------------------- */
/// @brief 找出 a, b 中的最大值
///
/// @tparam T 必需实现大于, 小于运算符
///
template<typename T> 
T MAX(const T &a, const T &b) {
    return a > b ? a : b;
}


/* -------------------------------------- */
/// @brief 找出 a, b 中的最小值
///
/// @tparam T 必需实现大于, 小于运算符
///
template<typename T>
T MIN(const T& a, const T& b) {
    return a < b ? a : b;
}


/* -------------------------------------- */
/// @brief 找出 a, b, c 中的中间值
///
/// @tparam T 必需实现大于, 小于运算符
///
template<typename T>
T MID(const T& a, const T& b, const T& c) {
    return MIN(MAX(a, b), c);
}


// ---------------------------------------------------------------------------- 时间 ----------------------------------------------------------------------------

/* -------------------------------------- */
/// @brief 获取当前时间(秒)
///
inline int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}


/* -------------------------------------- */
/// @brief 获取当前时间(毫秒)
///
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}


/* -------------------------------------- */
/// @brief 获取当前时间(微秒)
///
inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}


/* -------------------------------------- */
/// @brief 获取当前时间(纳秒)
///
inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
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


/* -------------------------------------- */
/// @brief 反转 u16
///
inline uint16_t __rvs_u16(uint16_t v) {
    return (v << 8) | (v >> 8);
}


/* -------------------------------------- */
/// @brief 反转 u32
///
inline uint32_t __rvs_u32(uint32_t v) {
    return (((uint32_t)(__rvs_u16(v)) << 16)) | (__rvs_u16(v >> 16));
}


/* -------------------------------------- */
/// @brief 反转 u64
///
inline uint64_t __rvs_u64(uint64_t v) {
    return (((uint64_t)(__rvs_u32(v)) << 32)) | (__rvs_u32(v >> 32));
}


/* -------------------------------------- */
/// @brief 当前平台是否为小端序平台
///
inline bool is_le() {
    return !X_BIG_ENDIAN;
}


/* -------------------------------------- */
/// @brief 当前平台是否为大端序平台
///
inline bool is_be() {
    return X_BIG_ENDIAN;
}


/* -------------------------------------- */
/// @brief 将 u16 转换为小端序
///
inline uint16_t to_le_u16(uint16_t v) {
#if X_BIG_ENDIAN
    return __rvs_u16(v);
#else
    return v;
#endif // X_BIG_ENDIAN

}


/* -------------------------------------- */
/// @brief 将 u16 转换为大端序
///
inline uint16_t to_be_u16(uint16_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u16(v);
#endif // X_BIG_ENDIAN
}


/* -------------------------------------- */
/// @brief 将 u32 转换为小端序
///
inline uint32_t to_le_u32(uint32_t v) {
#if X_BIG_ENDIAN
    return __rvs_u32(v);
#else
    return v;
#endif // X_BIG_ENDIAN
}


/* -------------------------------------- */
/// @brief 将 u32 转换为大端序
///
inline uint32_t to_be_u32(uint32_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u32(v);
#endif // X_BIG_ENDIAN
}


/* -------------------------------------- */
/// @brief 将 u64 转换小端序
///
inline uint64_t to_le_u64(uint64_t v) {
#if X_BIG_ENDIAN
    return __rvs_u64(v);
#else
    return v;
#endif // X_BIG_ENDIAN
}


/* -------------------------------------- */
/// @brief 将 u64 转换为大端序
///
inline uint64_t to_be_u64(uint64_t v) {
#if X_BIG_ENDIAN
    return v;
#else
    return __rvs_u64(v);
#endif // X_BIG_ENDIAN
}


/* -------------------------------------- */
/// @brief 二进制数据转换为16进制字符串
///
std::string bin2hex(const uint8_t* data, size_t data_len) {
    ASSERT(data);

    if (!data_len)
        return "";

    std::string res(data_len * 2, 0);
    uint8_t tmp;

    for (size_t i = 0; i < data_len; i++) {
        tmp = data[i];
        for (size_t j = 0; j < 2; j++) {
            uint8_t c = (tmp & 0x0f);
            if (c < 10) c += '0';
            else c += ('A' - 10);

            res[2 * i + 1 - j] = c;
            tmp >>= 4;
        }
    }
    return res;
}


/* -------------------------------------- */
/// @brief 将16进制字符串转换为二进制数据
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
            if (c >= '0' && c <= '9') tmp = (tmp << 4) + (c - '0');
            else if (c >= 'a' && c <= 'f') tmp = (tmp << 4) + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') tmp = (tmp << 4) + (c - 'A' + 10);
            else return -1;
        }
        data[i] = tmp;
    }

    *data_len = n;
    return (int)n;
}

// ---------------------------------------------------------------------------- spin lock  ----------------------------------------------------------------------------

/* -------------------------------------- */
/// @brief 通过atomic使用的自旋锁
///
/// @note 经测试, 性能比std::mutex高出许多
///
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


// ---------------------------------------------------------------------------- safe hash set ----------------------------------------------------------------------------

/* -------------------------------------- */
/// @brief 线程安全的 std::unordered_set
///
template <typename T>
class Set {
public:
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


    /* -------------------------------------- */
    /// @brief 获取set的 std::vector 副本
    ///
    size_t as_vec(std::vector<T> &vec) {
        mtx_.lock();
        std::copy(m_.begin(), m_.end(), std::back_inserter(vec));
        mtx_.unlock();
        return vec.size();
    }


    auto find(const T& v) {
        std::lock_guard<std::mutex> lk(mtx_);
        return m_.find(v);
    }


private:
    std::mutex mtx_;
    std::unordered_set<T> m_;


    Set(const Set&) = delete;
    Set(const Set&&) = delete;
    Set& operator=(const Set&) = delete;
    Set& operator=(const Set&&) = delete;
}; // class Set;


// ---------------------------------------------------------------------------- safe hash map ----------------------------------------------------------------------------

/* -------------------------------------- */
/// @brief 线程安线的 hash map
/// @tparam TLock 锁类型, 默认为 std::mutex
///
template <typename TKey, typename TVal, typename TLock = std::mutex>
class Map {
public:
    Map() = default;
    ~Map() = default;
    Map(const Map&) = delete;
    Map(const Map&&) = delete;
    Map& operator=(const Map&) = delete;
    Map& operator=(const Map&&) = delete;


    /* -------------------------------------- */
    /// @brief 获取map中所有的值到 vals中
    ///
    /// @param vals
    ///
    /// @param n 
    ///
    size_t get_all_vals(TVal* vals, size_t n) {
        size_t i = 0;
        std::lock_guard<TLock> lk(mtx_);
        ASSERT(n >= m_.size());

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
}; // class Map;


// ---------------------------------------------------------------------------- BTreeTimer  ----------------------------------------------------------------------------

/* -------------------------------------- */
/// @brief 红黑树定时器
///
class BTreeTimer final {
public:
    typedef void(*TimerHandler)(void* arg);


    // ------------------------------------------------------------------ 定时任务 ------------------------------------------------------------------
    /// @brief 
    ///
    /// @note 该类型由BTreeTimer 内部维护, 所以构造函数为私有, 并且通过 new运算符 创建
    ///       的对象由 BTreeTimer内部维护, 外部函数千万不要 delete 该对象.
    ///
    struct Timer {
        int32_t ntimes;       // 执行次数
        int64_t expire_ms;    // 超时值
        int64_t interval;     // 时间间隔
        TimerHandler handler; // 定时器事件
        void* arg;            // 事件参数


        /* -------------------------------------- */
        /// @brief 取消定时任务
        ///
        void cancel() {
            handler = nullptr;
        }


    private:
        /* -------------------------------------- */
        /// @brief Timer私有构造函数, 是为了不让在外部创建该Timer
        ///
        Timer(int64_t expire_ms, TimerHandler handler, void* arg, int64_t interval = 0, int32_t ntimes = 0)
            : ntimes(ntimes)
            , expire_ms(expire_ms)
            , interval(interval)
            , handler(handler)
            , arg(arg)
        {}


        friend class BTreeTimer;
        Timer(const Timer&) = delete;
        Timer(const Timer&&) = delete;
        Timer& operator=(const Timer&) = delete;
        Timer& operator=(const Timer&&) = delete;
    };
    // ------------------------------------------------------------------ 定时任务 ------------------------------------------------------------------


    typedef std::shared_ptr<BTreeTimer> Ptr;
    typedef std::unordered_set<Timer*> Slot;


    static Ptr create() {
        return Ptr(new BTreeTimer);
    }


    ~BTreeTimer() {
        if (running_) {
            stop();
        }

        if (sch_.joinable()) {
            wait();
        }
    }


    /* -------------------------------------- */
    /// @brief 开启定时调度
    ///
    /// @param interval 调度间隔
    ///
    void start(int interval = 10) {
        running_ = true;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            int64_t now_ms = xq::tools::now_ms();

            while (1) {
                mtx_.lock();
                auto slot_itr = slotm_.begin();
                if (slot_itr == slotm_.end()) {
                    mtx_.unlock();
                    break;
                }
                mtx_.unlock();

                if (slot_itr->first > now_ms) {
                    break;
                }

                Slot* slot = slot_itr->second;
                for (auto& timer_itr : *slot) {
                    Timer* timer = timer_itr;
                    if (timer->handler) {
                        timer->handler(timer->arg);
                    }

                    if (timer->ntimes > 0) {
                        timer->ntimes--;
                    }

                    if (timer->interval == 0 || timer->ntimes == 0) {
                        delete timer;
                    }
                    else {
                        timer->expire_ms += timer->interval;
                        _create_timer_at(timer);
                    }
                }

                delete slot;
                mtx_.lock();
                slotm_.erase(slot_itr++);
                mtx_.unlock();
            }
        }
    }


    /* -------------------------------------- */
    /// @brief 异步开启定时调度
    ///
    /// @param interval 调度间隔
    ///
    void run(int interval = 10) {
        running_ = true;
        sch_ = std::thread(std::bind(&BTreeTimer::start, this, interval));
    }


    /* -------------------------------------- */
    /// @brief 停止调度
    ///
    void stop() {
        running_ = false;
    }


    /* -------------------------------------- */
    /// @brief 等待异步调度结束
    ///
    void wait() {
        sch_.join();

        for (auto slot_itr = slotm_.begin(); slot_itr != slotm_.end();) {
            Slot* slot = slot_itr->second;
            for (auto timer_itr = slot->begin(); timer_itr != slot->end();) {
                Timer* timer = *timer_itr;
                delete timer;
                slot->erase(timer_itr++);
            }

            delete slot_itr->second;
            slotm_.erase(slot_itr++);
        }
    }


    /* -------------------------------------- */
    /// @brief 创建定时任务
    ///
    /// @param expir_ms 绝对时间(毫秒)
    ///
    /// @param handler 定时器事件
    ///
    /// @param arg 事件参数
    ///
    /// @return 返回创建好的定器指针
    ///
    /// @note 返回的定时器指针由调度器管理
    ///
    Timer* create_timer_at(int64_t expir_ms, TimerHandler handler, void* arg) {
        if (!handler) {
            return nullptr;
        }

        if (expir_ms <= xq::tools::now_ms()) {
            handler(arg);
            return nullptr;
        }

        return _create_timer_at(new Timer(expir_ms, handler, arg));
    }


    /* -------------------------------------- */
    /// @brief 创建定时任务
    ///
    /// @param delay_ms 相对时间(毫秒)
    ///
    /// @param handler 定时器事件
    ///
    /// @param arg 事件参数
    ///
    /// @param loop 是否循环执行; loop 为真时, 将每隔delay_ms 执行一次定时任务. 否则只执行一次.
    /// 
    /// @param ntimes 执行次数, 当该loop 为真时忽略该值.
    ///
    /// @return 返回创建好的定器指针
    ///
    /// @note 返回的定时器指针由调度器管理
    ///
    Timer* create_timer_after(int64_t delay_ms, TimerHandler handler, void* arg, bool loop = false, int32_t ntimes = 0) {
        if (delay_ms <= 0) {
            return nullptr;
        }

        if (loop) {
            ntimes = -1;
        }

        return _create_timer_at(
            new Timer(xq::tools::now_ms() + delay_ms, handler, arg, delay_ms, ntimes)
        );
    }


private:
    BTreeTimer()
        : running_(false)
    {}


    Timer* _create_timer_at(Timer* timer) {
        mtx_.lock();
        auto itr = slotm_.find(timer->expire_ms);
        if (itr != slotm_.end()) {
            itr->second->insert(timer);
        }
        else {
            Slot* slot = new Slot;
            slot->insert(timer);
            slotm_.insert(std::make_pair(timer->expire_ms, slot));
        }
        mtx_.unlock();

        return timer;
    }


    volatile bool running_;
    std::thread sch_;
    std::mutex mtx_;
    std::map<int64_t, Slot*> slotm_;


    BTreeTimer(const BTreeTimer&) = delete;
    BTreeTimer(const BTreeTimer&&) = delete;
    BTreeTimer& operator=(const BTreeTimer&) = delete;
    BTreeTimer& operator=(const BTreeTimer&&) = delete;
}; // class BTreeTimer;


} // namespace tools
} // namespace xq


#endif // __XQ_TOOLS_HPP__
