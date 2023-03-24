#include "xq/tools/tools.hpp"
#include <set>


using BTreeTimer = xq::tools::BTreeTimer;
BTreeTimer::Ptr btmr = BTreeTimer::create();

int64_t beg;
constexpr int NTIMES = 200000;

int main(int, char**) {
    btmr->run();
    beg = xq::tools::now();

    btmr->create_timer_after(2000, [](void*) {
        std::printf("---------------------------------------------------------------------: %lld\n", xq::tools::now());
        }, nullptr, true);

    for (int i = 1; i <= NTIMES; i++) {
        btmr->create_timer_after((i + 1) * 20, [](void* num) {
            int n = (int)num;
            if (n == NTIMES) {
                std::printf("take %lld s\n", xq::tools::now() - beg);
                btmr->stop();
            }
            std::printf("--- %d ---\n", (int)n);
        }, (void*)i);
    }

    btmr->wait();
    exit(EXIT_SUCCESS);
}