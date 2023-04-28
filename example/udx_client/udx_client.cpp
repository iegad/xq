#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using Udx = xq::net::Udx<>;
static int COUNT = 0;
static Udx::ptr udx;

class EchoEvent {
public:
    using UdpSession = xq::net::UdpSession<EchoEvent>;
    using Datagram = xq::net::Datagram;


    static UdpSession* Session() {
        static UdpSession::Ptr sess;
        if (!sess) {
            sess = UdpSession::create();
        }
        return sess.get();
    }


    int on_recv(UdpSession* sess, const Datagram* dg) {
        static uint8_t* rbuf = new uint8_t[xq::net::UDX_MSG_MAX];

        int n = udx->input(dg->data, dg->datalen, &dg->name, dg->namelen, dg->time_us);
        if (n < 0) {
            std::printf("input failed: %d\n", n);
            return -1;
        }

        n = udx->recv(rbuf, xq::net::UDX_MSG_MAX);
        if (n > 0) {
            rbuf[n] = 0;
            std::printf("%s\n", (char*)rbuf);
            if (COUNT == 1000) {
                udx->flush(dg->time_us);
                sess->stop();
                return 0;
            }

            n = sprintf((char*)rbuf, "Hello world: %d", COUNT++);
            udx->send(rbuf, n);
        }

        udx->flush(dg->time_us);
        return 0;
    }


    static int output(const Datagram::ptr *dgs, int ndg) {
        return EchoEvent::Session()->flush(dgs, ndg);
    }
};



int main(int argc, char** argv) {
#ifdef WIN32
    WSADATA wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) {
        exit(EXIT_FAILURE);
    }
#endif // WIN32
    auto sess = EchoEvent::Session();
    sess->run();
    udx = Udx::create(1, &EchoEvent::output);
    udx->connect("1.15.81.179:6688");
    char buf[10000] = {0};
    int64_t beg = xq::tools::now_ms();
    sprintf(buf, "Hello world: %d", COUNT++);
    int n = udx->send((uint8_t*)buf, strlen(buf));
    if (n < 0) {
        std::printf("send failed: %d\n", n);
    }
    
    udx->flush(xq::tools::now_us());
    sess->wait();
    sess->close();
    std::printf(">>>> takes %lld ms\n", xq::tools::now_ms() - beg);

#ifdef WIN32
    WSACleanup();
#endif // WIN32
    exit(EXIT_SUCCESS);
}