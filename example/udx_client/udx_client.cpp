#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;


static UdpSession::Ptr sess;
static Udx::Ptr udx;


int rcv_cb(const UdpSession::Datagram* dg) {
    std::printf("[%lld]:%s\n", dg->time_ms, xq::tools::bin2hex(dg->data, dg->datalen).c_str());

    int n = udx->input(dg->data, dg->datalen, dg->time_ms);
    if (n < 0) {
        std::printf("input failed: %d\n", n);
    }

    return 0;
}


void send_wkr() {
    char buf[xq::net::UDX_MSS] = { 0 };
    sockaddr addr = { 0,{0} };
    socklen_t addrlen = sizeof(addr);

    for (int i = 0; i < 1000; i++) {
        sprintf(buf, "Hello world: %d", i);
        int n = udx->send((uint8_t*)buf, strlen(buf));
        if (n < 0) {
            std::printf("send failed: %d\n", n);
        }
        udx->flush(xq::tools::now_ms());
    }
}


int main(int argc, char** argv) {
#ifdef WIN32
    WSADATA wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) {
        exit(EXIT_FAILURE);
    }
#endif // WIN32
    sess = UdpSession::create("192.168.0.201:0");
    udx = Udx::create(1, sess);
    udx->set_addr("192.168.0.201:6688");
    std::thread(send_wkr).detach();
    sess->start_rcv(rcv_cb);

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}