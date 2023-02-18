#define KL_EVENT_ON_START        1
#define KL_EVENT_ON_STOP         0
#define KL_EVENT_ON_CONNECTED    1
#define KL_EVENT_ON_RECONNECTED  0
#define KL_EVENT_ON_DISCONNECTED 1
#define KL_EVENT_ON_SEND         0
#define KL_EVENT_ON_RECV         0


#include "xq/tools/tools.hpp"
#include "xq/net//kcp_listener.hpp"
#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif


class EchoEvent {
public:
    typedef xq::net::KcpListener<EchoEvent> KcpListener;
    typedef KcpListener::Sess KcpSess;

    int on_message(KcpSess* sess, const uint8_t* data, size_t datalen) {
        return sess->send(data, datalen);
    }

    int on_connected(KcpSess* sess)  {
        std::printf("+++ %u has connected\n", sess->get_conv());
        return 0;
    }

    int on_reconnected(KcpSess* sess) {
        std::printf("### %u has reconnected\n", sess->get_conv());
        return 0;
    }

    void on_disconnected(KcpSess * sess) {
        std::printf("--- %u has disconnected\n", sess->get_conv());
    }

    void on_error(xq::net::ErrType err_type, int err, void* /*arg*/) {
        std::printf("ErrType: %d, ErroCode: %d\n", (int)err_type, err);
    }

    int on_start(KcpListener* listener) {
        std::printf("%s is running...\n", listener->host().c_str());
        return 0;
    }

    void on_stop(KcpListener* listener) {
        std::printf("%s has stopped...\n", listener->host().c_str());
    }

    int on_recv(uint8_t*, size_t, const sockaddr*, socklen_t) {
        return 0;
    }

    void on_send(const uint8_t*, size_t, const sockaddr*, socklen_t) {

    }
};


constexpr char HOST[] = ":6688";

int
main(int, char**) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32
	EchoEvent::KcpListener::Ptr listener = EchoEvent::KcpListener::create(HOST, 2000);
	listener->start();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
