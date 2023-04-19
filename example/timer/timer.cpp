#include "xq/tools/tools.hpp"
#include <set>


using BTreeTimer = xq::tools::BTreeTimer;
BTreeTimer::Ptr btmr = BTreeTimer::create();

int64_t beg;
constexpr int NTIMES = 200000;


int main(int, char**) {
    btmr->run(5);
    beg = xq::tools::now_ms();

    btmr->create_timer_after(2000, [](void*) {
        std::printf("---------------------------------------------------------------------: %ld\n", xq::tools::now());
    }, nullptr, -1);

    btmr->create_timer_after(10000, [](void*) {
        std::printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++: %ld\n", xq::tools::now());
    }, nullptr, 10);

    for (long i = 1; i <= NTIMES; i++) {
        int64_t delay_ms = (i + 1) * 20;
        btmr->create_timer_after(delay_ms, [](void* num) {
            long n = long(num);
            std::printf(">>> %ld <<<\n", n);
        }, (void*)i);

        btmr->create_timer_after(delay_ms, [](void* num) {
            long n = long(num);
            std::printf("+++ %ld +++\n", n);
        }, (void*)i);

        btmr->create_timer_after(delay_ms, [](void* num) {
            long n = long(num);
            std::printf("@@@ %ld @@@\n", n);
        }, (void*)i);

        btmr->create_timer_after(delay_ms, [](void* num) {
            long n = long(num);
            std::printf("--- %ld ---\n", n);
            if (n == NTIMES) {
                std::printf("take %ld ms\n", xq::tools::now_ms() - beg);
                btmr->stop();
            }
        }, (void*)i);
    }

    btmr->wait();
    exit(EXIT_SUCCESS);
}