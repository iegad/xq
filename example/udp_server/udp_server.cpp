#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/udp_session.hpp"


class EchoEvent;
using UdpSession = xq::net::UdpSession<EchoEvent>;
using Datagram = xq::net::Datagram;
static UdpSession::Ptr server;


class EchoEvent {
public:
    int on_recv(UdpSession* sess, const Datagram* dg) {
        static int count = 0;
        count++;
        auto p = *(Datagram**)&dg;
        p->data[p->datalen] = 0;
        std::printf("%d => %s\n", count, (char *)p->data/*xq::tools::bin2hex(dg->data, dg->datalen).c_str()*/);
        return 0;
    }


    int on_send(const Datagram* dg) {

    }


    int on_error(int err_type, int err_code, void *ev) {

    }
};


void signal_handler(int signal) {
    if (signal == SIGINT && server) {
        server->stop();
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
    EchoEvent ev;
    server = UdpSession::create(":6688", ev);
    server->run("224.0.0.10", "192.168.0.201");
    server->wait();
    std::printf("EXIT.!!!\n");

#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
    exit(0);
}
