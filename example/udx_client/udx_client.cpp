#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;


static UdpSession::Ptr sess;
static Udx::Ptr udx;
static int COUNT = 0;


int rcv_cb(const UdpSession::Datagram* dg) {
    static uint8_t* rbuf = new uint8_t[xq::net::UDX_MSG_MAX];

    int n = udx->input(dg->data, dg->datalen, &dg->name, dg->namelen, dg->time_ms);
    if (n < 0) {
        std::printf("input failed: %d\n", n);
        return -1;
    }
    
    n = udx->recv(rbuf, xq::net::UDX_MSG_MAX);
    if (n > 0) {
        rbuf[n] = 0;
        std::printf("%s\n", (char*)rbuf);
        if (COUNT == 1000) {
            udx->flush(dg->time_ms);
            sess->stop();
            return 0;
        }

        n = sprintf((char*)rbuf, "Hello world: %d", COUNT++);
        udx->send(rbuf, n);
    }

    udx->flush(dg->time_ms);
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
    udx = Udx::create(1, *sess);
    //udx->connect("1.15.81.179:6688");
    udx->connect("192.168.0.201:6688");
    char buf[10000] = {0};
    int64_t beg = xq::tools::now_ms();
    sprintf(buf, "Hello world: %d", COUNT++);
    int n = udx->send((uint8_t*)buf, strlen(buf));
    if (n < 0) {
        std::printf("send failed: %d\n", n);
    }

    udx->flush(xq::tools::now_ms());
    sess->start_rcv(rcv_cb);
    sess->close();
    std::printf(">>>> takes %lld ms\n", xq::tools::now_ms() - beg);

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}