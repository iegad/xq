#include "tools.hpp"
#include <assert.h>

std::string 
xq::tools::bin2hex(const uint8_t* data, size_t data_len) {
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

void 
xq::tools::SpinMutex::lock() {
    for (;;) {
        if (!mtx_.exchange(true, std::memory_order_acquire))
            break;

        while (mtx_.load(std::memory_order_relaxed)) {
#ifdef _WIN32
            _mm_pause();
#else
            __builtin_ia32_pause();
#endif // _WIN32
        }
    }
}
