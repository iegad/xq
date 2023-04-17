#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;

static UdpSession::Ptr sess;


int rcv_cb(const UdpSession::Datagram* udp_seg) {
    return 0;
}


void send_wkr() {
    Udx::Segment* seg0 = Udx::Segment::new_con(1, 0, 0, 4, (uint8_t*)"1111111111", 10);
    Udx::Segment* seg1 = Udx::Segment::new_psh(1, 1, 1, 3, (uint8_t*)"2222222222", 10);
    Udx::Segment* seg2 = Udx::Segment::new_psh(1, 2, 2, 2, (uint8_t*)"3333333333", 10);
    Udx::Segment* seg3 = Udx::Segment::new_psh(1, 3, 3, 1, (uint8_t*)"4444444444", 10);
    Udx::Segment* seg4 = Udx::Segment::new_psh(1, 4, 4, 0, (uint8_t*)"5555555555", 10);
    
    
    sockaddr addr = { 0,{0} };
    socklen_t addrlen = sizeof(addr);

    xq::net::str2addr("192.168.0.201:6688", &addr, &addrlen);

    uint8_t buf[xq::net::UDX_MTU];

    buf[0] = 1;
    buf[1] = 0;
    buf[2] = 0;

    uint8_t* p = buf + 3;

    int n = seg0->encode(p, xq::net::UDX_MTU) + 3;
    UdpSession::Datagram* dg1 = UdpSession::Datagram::get(nullptr, &addr, addrlen, buf, n);
    

    n = seg1->encode(p, xq::net::UDX_MTU) + 3;
    UdpSession::Datagram* dg2 = UdpSession::Datagram::get(nullptr, &addr, addrlen, buf, n);

    n = seg2->encode(p, xq::net::UDX_MTU) + 3;
    UdpSession::Datagram* dg3 = UdpSession::Datagram::get(nullptr, &addr, addrlen, buf, n);

    n = seg3->encode(p, xq::net::UDX_MTU) + 3;
    UdpSession::Datagram* dg4 = UdpSession::Datagram::get(nullptr, &addr, addrlen, buf, n);

    n = seg4->encode(p, xq::net::UDX_MTU) + 3;
    UdpSession::Datagram* dg5 = UdpSession::Datagram::get(nullptr, &addr, addrlen, buf, n);

    sess->send(dg1, true);
    sess->send(dg5, true);
    sess->send(dg4, true);
    sess->send(dg2, true);
    sess->send(dg3, true);
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