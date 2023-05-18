#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/udp_session.hpp"


class EchoEvent {
public:
    using Frame = xq::net::Frame;
    typedef xq::net::UdpSession<EchoEvent> UdpSession;


    int on_recv(UdpSession* sess, const Frame* frm) {
        static int count = 0;
        count++;
        std::printf("%d => %s\n", count, xq::tools::bin2hex(frm->raw, frm->rawlen).c_str());
        return 0;
    }


    int on_send(const Frame* dg) {
        return 0;
    }


    int on_error(int err_type, int err_code, void *ev) {
        return 0;
    }
};

using UdpSession = xq::net::UdpSession<EchoEvent>;
static UdpSession::Ptr server;


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
    server = UdpSession::create(":6688");
    server->run();
    server->wait();
    std::printf("EXIT.!!!\n");

#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
    exit(0);
}
