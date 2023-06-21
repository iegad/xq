#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif

#include "xq/net/rux_server.hpp"
#include "xq/net/udx.hpp"


class EchoEvent {
public:
    inline void on_error(xq::net::ErrType et, void* arg) {
        switch (et) {
        case xq::net::ErrType::IO_RCV:
            DLOG("IO_RCV: %d\n", (int)((int64_t)arg));
            break;
        case xq::net::ErrType::IO_RCV_FRAME:
            break;
        case xq::net::ErrType::IO_SND:
            DLOG("IO_SND: %lld\n", (int64_t)arg);
            break;
        case xq::net::ErrType::RUX_INPUT:
            break;
        case xq::net::ErrType::RUX_OUTPUT:
            break;
        default:
            break;
        }
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



void
on_recv_frame(xq::net::Udx *udx, int err, xq::net::Frame::ptr pfm) {
    if (err != 0) {
        DLOG("error: %d\n", err);
        return;
    }

    pfm->raw[pfm->len] = 0;
    DLOG("%s\n", (char*)pfm->raw);
}



void
on_prev_run(xq::net::Udx* udx) {
    ASSERT(!udx->join_multicast(udx->host(), "224.0.0.88"));
}


int 
main(int argc, char** argv) {
    ASSERT(!rux_env_init());
    //xq::net::RuxServer<EchoEvent> server;
    //server.run("0.0.0.0", "6688");
    //server.wait();

    xq::net::UdxOption opt;
    opt.endpoint = "192.168.0.201:6688";
    opt.prev_run_handler = on_prev_run;
    opt.recv_frame_handler = on_recv_frame;

    xq::net::Udx udx(&opt);
    udx.run();
    udx.wait();
    ASSERT(!rux_env_release());
    return 0;
}