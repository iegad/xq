#include <iostream>
#include "xq/net/rux.hpp"

int
main(int, char**) {
    sockaddr_storage addr;
    ::memset(&addr, 0, sizeof(addr));
    moodycamel::BlockingConcurrentQueue<xq::net::PRUX_FRM> que;
    int64_t now_us = sys_clock();
    xq::net::Rux rux(1, sys_clock(), que);
    rux.active(now_us, &addr, sizeof(addr));
    rux.send((uint8_t*)"Hello world", 11);
    rux.flush(sys_clock());
    xq::net::PRUX_FRM frms[128];
    int n = que.wait_dequeue_bulk(frms, 128);

    int len = frms[0]->len << 1;
    char *buf = new char[len + 1];
    n = bin2hex(frms[0]->raw, frms[0]->len, buf, len);
    buf[len] = 0;
    std::printf("%s\n", buf);
    exit(EXIT_SUCCESS);
}