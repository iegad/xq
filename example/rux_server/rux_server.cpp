#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include "xq/net/rux_server.hpp"


class EchoEvent {
public:
    inline void on_error(xq::net::ErrType et, void* arg) {
        DLOG("error: %d\n", (int)et);
        //switch (et) {
        //case xq::net::ErrType::IO_RCV:
        //    break;
        //case xq::net::ErrType::IO_RCV_FRAME:
        //    break;
        //case xq::net::ErrType::IO_SND:
        //    DLOG("IO_SND: %lld\n", (int64_t)arg);
        //    break;
        //case xq::net::ErrType::RUX_INPUT:
        //    break;
        //case xq::net::ErrType::RUX_OUTPUT:
        //    break;
        //default:
        //    break;
        //}
    }

    inline void on_message(xq::net::Rux* rux, const uint8_t *msg, int msglen) {
        if (rux->send(msg, msglen) < 0) {
            DLOG("---------------------------------------------- send failed ------------\n");
        }
    }

    inline int on_connected(xq::net::Rux* rux) {
        DLOG("%u connected\n", rux->rid());
        return 0;
    }

    inline void on_disconnected(xq::net::Rux* rux) {
        DLOG("%u disconnected\n", rux->rid());
    }

    inline void on_rcv_frame(const xq::net::PRUX_FRM frm) {
        //static char buf[xq::net::RUX_MTU * 2 + 1];
        //int n = bin2hex(frm->raw, frm->len, buf, xq::net::RUX_MTU * 2 + 1);
        //buf[n] = 0;
        //DLOG("%s\n", buf);
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