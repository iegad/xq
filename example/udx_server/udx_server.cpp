#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include <iostream>
#include "xq/net/udx.hpp"


using Udx = xq::net::Udx<>;


class EchoEvent {
public:
    using Datagram = xq::net::Datagram;
    using UdpSession = xq::net::UdpSession<EchoEvent>;


    static UdpSession* Session() {
        static UdpSession::Ptr sess;
        if (!sess) {
            sess = UdpSession::create(":6688");
        }
        return sess.get();
    }


    int on_recv(UdpSession* sess, const Datagram* dg) {
        static uint8_t* rbuf = new uint8_t[xq::net::UDX_MSG_MAX];

        Udx* udx = Udx::Sets::instance()->load(dg->data, dg->datalen, &EchoEvent::output);
        if (!udx) {
            return 0;
        }

        int n = udx->input(dg->data, dg->datalen, &dg->name, dg->namelen, dg->time_us);
        if (n < 0) {
            std::printf("input failed: %d\n", n);
            return 0;
        }

        while (n = udx->recv(rbuf, xq::net::UDX_MSG_MAX), n > 0) {
            rbuf[n] = 0;
            //std::printf("\n\n--------------------------------------------\n");
            //std::printf("%s\n", (char*)rbuf);
            udx->set_addr(&dg->name, dg->namelen);
            ASSERT(!udx->send(rbuf, n));
            udx->flush(dg->time_us);
        }

        return 0;
    }


    static int output(const Datagram::ptr *dgs, int ndg) {
        return EchoEvent::Session()->flush(dgs, ndg);
    }


    EchoEvent() {
        Udx::Timer()->run();
    }


    ~EchoEvent() {
        Udx::Timer()->stop();
    }
};


void signal_handler(int signal) {
    if (signal == SIGINT) {
        EchoEvent::Session()->stop();
    }
}


int main(int, char**) {
#ifdef _WIN32
    WSAData wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202) {
        exit(1);
    }
#endif // _WIN32

    std::signal(SIGINT, signal_handler);
    auto sess = EchoEvent::Session();
    sess->run();
    sess->wait();
    std::printf("EXIT.!!!\n");

#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
    return 0;
}
