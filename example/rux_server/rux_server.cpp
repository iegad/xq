#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include "xq/net/rux_server.hpp"


class EchoEvent {
public:
    inline void on_error(xq::net::ErrType et, void* arg) {

    }

    inline void on_message(xq::net::Rux* rux, const uint8_t *msg, int msglen) {
        (*(uint8_t**)&msg)[msglen] = 0;
        // DLOG("%s\n", (char*)msg);
    }

    inline int on_connected(xq::net::Rux* rux) {
        DLOG("%u connected\n", rux->rid());
        return 0;
    }

    inline void on_disconnected(xq::net::Rux* rux) {
        DLOG("%u disconnected\n", rux->rid());
    }

    inline void on_rcv_frame(const xq::net::PRUX_FRM frm) {
        DLOG(" --------------- RID[%llu], WND[%d], UNA[%llu]\n", frm->rid, frm->wnd, frm->una);
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