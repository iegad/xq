#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include "xq/net/rux_server.hpp"


class EchoEvent {
public:
    void on_error(xq::net::ErrType et, void* arg) {

    }

    void on_message(xq::net::Rux* rux, const uint8_t *msg, int msglen) {
        (*(uint8_t**)&msg)[msglen] = 0;
        DLOG("%s\n", (char*)msg);
    }

    int on_connected(xq::net::Rux* rux) {
        DLOG("%u connected\n", rux->rid());
        return 0;
    }

    void on_disconnected(xq::net::Rux* rux) {
        DLOG("%u disconnected\n", rux->rid());
    }
};


int 
main(int argc, char** argv) {
    ASSERT(!rux_env_init());
    xq::net::RuxServer<EchoEvent> server;
    server.run("0.0.0.0", "6688");
    server.wait();
    ASSERT(!rux_env_release());
    return 0;
}