#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;


static UdpSession::Ptr sess;
static Udx::Ptr udx;
static int COUNT = 0;


int rcv_cb(const UdpSession::Datagram* dg) {
    std::printf("[%lld]:%s\n", dg->time_ms, xq::tools::bin2hex(dg->data, dg->datalen).c_str());

    int n = udx->input(dg->data, dg->datalen, dg->time_ms);
    if (n < 0) {
        std::printf("input failed: %d\n", n);
    }

    uint8_t* rbuf = new uint8_t[xq::net::UDX_MSG_MAX];
    n = udx->recv(rbuf, xq::net::UDX_MSG_MAX);
    if (n < 0) {
        std::printf("recv failed: %d\n", n);
    }
    else if (n > 0) {
        rbuf[n] = 0;
        std::printf("%s\n", (char*)rbuf);

        if (COUNT == 1000) {
            std::printf("----------------\n");
            sess->stop();
            return 0;
        }

        n = sprintf((char*)rbuf, "Hello world: %d", COUNT++);
        udx->send(rbuf, n);
        udx->flush(dg->time_ms);
    }

    delete[] rbuf;
    return 0;
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
    char buf[xq::net::UDX_MSS];
    sprintf(buf, "Hello world: %d", COUNT++);
    int n = udx->send((uint8_t*)buf, strlen(buf));
    if (n < 0) {
        std::printf("send failed: %d\n", n);
    }

    udx->flush_psh(xq::tools::now_ms());
    sess->start_rcv(rcv_cb);

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}