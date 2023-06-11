#ifndef __XQ_TIME_WHEEL__
#define __XQ_TIME_WHEEL__


#include <queue>
#include <thread>
#include <functional>
#include <xq/net/rux.in.h>


namespace xq {
namespace net {



template<class T>
struct __timer_cmp_ {
    typedef std::pair<uint64_t, T*> Timer;


    inline bool operator() (const Timer& a, const Timer& b) {
    return a.first > b.first;
    }
}; // struct __timer_cmp_;


template<class T>
class TimerScheduler {
public:
    typedef std::pair<uint64_t, T*> Timer;
    typedef std::priority_queue<Timer, std::vector<Timer>, __timer_cmp_<T>> MinHeap;


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


    inline void create_timer_at(uint64_t expire_us, T* ev) {
        if (expire_us <= sys_clock()) {
            ev->update();
            if (ev->destory()) {
                delete ev;
            }
        }

        lkr_.lock();
        que_.emplace(std::make_pair(expire_us + (INTERVAL - expire_us % INTERVAL), ev));
        lkr_.unlock();
    }


    inline void create_timer_after(int64_t delay_us, T* ev) {
        timer->set_expire_us();
        create_timer_at(sys_clock() + delay_us, ev);
    }


private:
    inline void _sleep() {
        constexpr int SLEEP_US = INTERVAL / 2;
        std::this_thread::sleep_for(std::chrono::microseconds(SLEEP_US));
    }


    void _run() {
        running_ = true;
        uint64_t now_us;
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
                const Timer &tmr = que_.top();
                lkr_.unlock();
                if (tmr.first > now_us) {
                    break;
                }

                T* ev = tmr.second;
                ev->update();
                if (ev->destory()) {
                    delete ev;
                }

                lkr_.lock();
                que_.pop();
                lkr_.unlock();
            }
        }
    }


    static constexpr int MAX = 1000;
    static constexpr int INTERVAL = 1000 * 10;

    bool        running_;
    MinHeap     que_;
    SPIN_LOCK   lkr_;
    std::thread thread_;
}; // class TimerScheduler;


} // namespace net;
} // namespace xq;


#endif // !__XQ_TIME_WHEEL__