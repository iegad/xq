#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/rux_server.hpp"


class EchoService {
public:
    void on_message(xq::net::Rux::ptr rux, uint8_t* msg, int msglen) {
        msg[msglen] = 0;
        //DLOG("%s\n", (char*)msg);
        rux->send(msg, msglen);
    }


    void on_connected(xq::net::Rux::ptr rux) {
        DLOG("%u has connected\n", rux->rid());
    }


    void on_disconnected(xq::net::Rux::ptr rux) {
        DLOG("%u has disconnected: srtt: %d, xmit: %lu\n", rux->rid(), rux->srtt(), rux->xmit());
    }
};


static xq::net::Udx<xq::net::RuxServer<EchoService>>* pUdx;

static void
signal_handler(int sig) {
    if (sig == SIGINT && pUdx) {
        pUdx->stop();
    }
}


int
main(int, char**) {
    ASSERT(!rux_env_init());
    ASSERT(std::signal(SIGINT, signal_handler) != SIG_ERR);

    EchoService es;
    xq::net::RuxServer server(&es);
    xq::net::Udx udx(":6688", &server);
    pUdx = &udx;
    udx.run(0);
    udx.wait();
    ASSERT(!rux_env_release());
    exit(0);
}
