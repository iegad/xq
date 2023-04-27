#include "xq/net/udp_session.hpp"
// #include "xq/net/udx.hpp"




// using Udx = xq::net::Udx;


class EchoEvent {
public:
    using Datagram = xq::net::Datagram;
    typedef xq::net::UdpSession<EchoEvent> UdpSession;

    int on_recv(UdpSession* sess, const Datagram* dg) {
        return 0;
    }

    int on_send(const Datagram* dg) {
        return 0;
    }

    int on_error(int err_type, int err_code, void* ev) {
        return 0;
    }
};


using UdpSession = EchoEvent::UdpSession;
static UdpSession::Ptr sess;


void send_wkr() {
    sockaddr addr = { 0,{0} };
    socklen_t addrlen = sizeof(addr);

    ASSERT(xq::net::str2addr("127.0.0.1:6688", &addr, &addrlen));

    /*Udx::Segment* seg0 = Udx::Segment::new_con(2, 0, 0, 0, (uint8_t*)"1111111111", 10);
    Udx::Segment* seg1 = Udx::Segment::new_psh(2, 1, 1, 0, (uint8_t*)"2222222222", 10);
    Udx::Segment* seg2 = Udx::Segment::new_psh(2, 2, 2, 0, (uint8_t*)"3333333333", 10);
    Udx::Segment* seg3 = Udx::Segment::new_psh(2, 3, 3, 0, (uint8_t*)"4444444444", 10);
    Udx::Segment* seg4 = Udx::Segment::new_psh(2, 4, 4, 0, (uint8_t*)"5555555555", 10);
    


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

    std::this_thread::sleep_for(std::chrono::seconds(10));*/

    char buf[1500] = { 0 };
    int n;
    for (int i = 0; i < 1000; i++) {
        n = ::sprintf(buf, "Hello world: %d", i + 1);
        sess->send((uint8_t*)buf, n, &addr, addrlen);
        sess->flush();
    }
}


int main(int argc, char** argv) {
#ifdef WIN32
    WSADATA wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) {
        exit(EXIT_FAILURE);
    }
#endif // WIN32
    sess = UdpSession::create("");
    std::thread t(send_wkr);
    t.join();

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}