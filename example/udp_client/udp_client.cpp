 #include "xq/net/udp_session.hpp"
 #include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;


static UdpSession::Ptr sess;


int rcv_cb(const UdpSession::Frame* udp_seg) {
    return 0;
}


void send_wkr() {
    char buf[xq::net::UDX_MSS] = { 0 };
    sockaddr addr = { 0,{0} };
    socklen_t addrlen = sizeof(addr);
    ASSERT(xq::net::str2addr("224.0.0.10:6688", &addr, &addrlen));

    for (int i = 0; i < 1000; i++) {
        sprintf(buf, "Hello world: %d", i);
        sess->send((uint8_t*)buf, strlen(buf), &addr, addrlen);
    }
    sess->flush();
}


int main(int argc, char** argv) {
#ifdef WIN32
    WSADATA wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) {
        exit(EXIT_FAILURE);
    }
#endif // WIN32
    sess = UdpSession::create("192.168.0.201:0");
    std::thread t(send_wkr);
    t.join();

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}