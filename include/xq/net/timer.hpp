#ifndef __XQ_TIME_WHEEL__
#define __XQ_TIME_WHEEL__


#include <queue>
#include <thread>
#include <functional>
#include <xq/net/rux.in.h>


namespace xq {
namespace net {


template<class T>
class Timer {
public:
    explicit Timer(uint64_t expire_us, T* handler) : expire_us_(expire_us), handler_(handler)
    {}

    inline operator uint64_t() {
    return expire_us_;
    }

    inline uint64_t expire_us() const {
    return expire_us_;
    }

    inline void set_expire_us(uint64_t v) {
    expire_us_ = v;
    }

    inline T* handler() {
    return handler_;
    }

private:
    uint64_t    expire_us_;
    T*          handler_;
}; // class Timer;


template<class T>
struct __timer_cmp_ {
    typedef Timer<T>* PTIMER;


    bool operator() (const PTIMER& a, const PTIMER& b) {
    return a->expire_us() > b->expire_us();
    }
}; // struct __timer_cmp_;


template<class T>
class TimerScheduler {
public:
    typedef Timer<T>* PTIMER;

    typedef std::priority_queue<PTIMER, std::vector<PTIMER>, __timer_cmp_<T>> MinHeap;


    TimerScheduler() : running_(false) {}


    inline void run(bool async = true) {
        if (!async) {
            _run();
        }

        thread_ = std::thread(std::bind(&TimerScheduler::_run, this));
    }


    inline void stop() {
        if (running_) {
            running_ = false;
        }
    }

    inline void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }


    inline void create_timer_at(PTIMER timer) {
        uint64_t expire_us = timer->expire_us();

        if (expire_us <= sys_clock()) {
            T* handler = timer->handler();
            handler->update();
            if (handler->destory()) {
                delete handler;
            }
        }

        timer->set_expire_us(expire_us + (INTERVAL - expire_us % INTERVAL));
        lkr_.lock();
        que_.emplace(timer);
        lkr_.unlock();
    }


    inline void create_timer_after(int64_t delay_us, PTIMER timer) {
        timer->set_expire_us(sys_clock() + delay_us);
        create_timer_at(timer);
    }


private:
    inline void _sleep() {
        constexpr int SLEEP_US = INTERVAL / 2;
        std::this_thread::sleep_for(std::chrono::microseconds(SLEEP_US));
    }


    void _run() {
        running_ = true;
        uint64_t now_us;
        PTIMER tmr;
        size_t n;

        while (running_) {
            _sleep();
            now_us = sys_clock();
            now_us = now_us - now_us % INTERVAL;

            while (1) {
                lkr_.lock();
                if (que_.size() == 0) {
                    lkr_.unlock();
                    break;
                }
                lkr_.unlock();
                
                lkr_.lock();
                tmr = que_.top();
                lkr_.unlock();
                if (*tmr > now_us) {
                    break;
                }

                T* handler = tmr->handler();
                handler->update();
                if (handler->destory()) {
                    delete handler;
                }

                lkr_.lock();
                que_.pop();
                lkr_.unlock();
                delete tmr;
            }
        }
    }


    static constexpr int MAX = 1000;
    static constexpr int INTERVAL = 1000 * 10;

    bool    running_;
    MinHeap que_;
    SPIN_LOCK lkr_;
    std::thread thread_;
}; // class TimerScheduler;


} // namespace net;
} // namespace xq;


#endif // !__XQ_TIME_WHEEL__