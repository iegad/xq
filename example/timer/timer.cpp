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
        std::printf("---------------------------------------------------------------------: %lld\n", xq::tools::now());
    }, nullptr, -1);

    btmr->create_timer_after(10000, [](void*) {
        std::printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++: %lld\n", xq::tools::now());
    }, nullptr, 10);

    for (int i = 1; i <= NTIMES; i++) {
        btmr->create_timer_after((i + 1) * 20, [](void* num) {
            int n = (int)num;
            std::printf(">>> %d <<<\n", (int)n);
        }, (void*)i, -1);
    }

    for (int i = 1; i <= NTIMES; i++) {
        btmr->create_timer_after((i + 1) * 20, [](void* num) {
            int n = (int)num;
            std::printf("+++ %d +++\n", (int)n);
            }, (void*)i, -1);
    }

    for (int i = 1; i <= NTIMES; i++) {
        btmr->create_timer_after((i + 1) * 20, [](void* num) {
            int n = (int)num;
            std::printf("=== %d ===\n", (int)n);
            }, (void*)i, -1);
    }

    for (int i = 1; i <= NTIMES; i++) {
        btmr->create_timer_after((i + 1) * 20, [](void* num) {
            int n = (int)num;
            std::printf("--- %d ---\n", (int)n);
            if (n == NTIMES) {
                std::printf("take %lld ms\n", xq::tools::now_ms() - beg);
                btmr->stop();
            }
        }, (void*)i, -1);
    }

    btmr->wait();
    exit(EXIT_SUCCESS);
}