#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include <iostream>
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;
static UdpSession::Ptr server;
static Udx::Ptr udx;



void signal_handler(int signal) {
    if (signal == SIGINT && server) {
        server->stop();
    }
}


int rcv_cb(const UdpSession::Datagram* dg) {
    std::printf("%s\n", xq::tools::bin2hex(dg->data, dg->datalen).c_str());
    int n = udx->input(dg->data, dg->datalen, xq::tools::now_ms());
    if (n < 0) {
        std::printf("%d\n", n);
    }
    uint8_t* rbuf = new uint8_t[xq::net::UDX_MSG_MAX];
    n = udx->recv(rbuf, xq::net::UDX_MSG_MAX);
    if (n < 0) {
        std::printf("%d\n", n);
    }

    rbuf[n] = 0;
    std::printf("%s\n", (char*)rbuf);

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
    udx = xq::net::Udx::create(1, server);
    server->run(rcv_cb);
    server->wait();
    std::printf("EXIT.!!!\n");

#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
    return 0;
}