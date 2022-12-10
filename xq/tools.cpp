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

int 
xq::tools::hex2bin(const std::string& hex, uint8_t* data, size_t* data_len) {
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
