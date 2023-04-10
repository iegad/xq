#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/udp_session.hpp"


using UdpSession = xq::net::UdpSession;
static UdpSession::Ptr server;


void signal_handler(int signal) {
    if (signal == SIGINT && server) {
        server->stop();
    }
}


int rcv_cb(const UdpSession::Frame* seg) {
    static int count = 0;
    count++;
    std::printf("%d => %s\n", count, seg->to_string().c_str());
    return 0;
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
    server->join_multi_addr("224.0.0.10", "192.168.0.201");
    server->run(rcv_cb);
    server->wait();
    std::printf("EXIT.!!!\n");

#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
    exit(0);
}
