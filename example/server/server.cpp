#define KL_EVENT_ON_START        1
#define KL_EVENT_ON_STOP         1
#define KL_EVENT_ON_CONNECTED    1
#define KL_EVENT_ON_RECONNECTED  1
#define KL_EVENT_ON_DISCONNECTED 1
#define KL_EVENT_ON_SEND         0
#define KL_EVENT_ON_RECV         0


#include "xq/net//kcp_listener.hpp"
#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include <csignal>


class EchoEvent {
public:
    typedef xq::net::KcpListener<EchoEvent> KcpListener;
    typedef KcpListener::Sess KcpSess;
    typedef KcpListener::Seg KcpSeg;

    int on_message(KcpSess* sess, const uint8_t* data, size_t datalen) {
        int n = sess->send(data, datalen);
        if (n < 0) {
            std::printf("kcp_send failed: %d\n", n);
        }

        return n;
    }

    int on_connected(KcpSess* sess)  {
        std::printf("+++ %u has connected\n", sess->conv());
        return 0;
    }

    int on_reconnected(KcpSess* sess) {
        std::printf("### %u has reconnected\n", sess->conv());
        return 0;
    }

    void on_disconnected(KcpSess * sess) {
        std::printf("--- %u has disconnected\n", sess->conv());
    }

    void on_error(xq::net::ErrType err_type, int err, void* /*arg*/) {
        std::printf("ErrType: %d, ErroCode: %d\n", (int)err_type, err);
    }

    int on_start(KcpListener* listener) {
        std::printf("%s is running...\n", listener->host().c_str());
        return 0;
    }

    void on_stop(KcpListener* listener) {
        std::printf("[%s] [sessions: %d] has stopped.\n", listener->host().c_str(), listener->conns());
    }

    int on_recv(KcpSeg*) {
        return 0;
    }

    void on_send(const uint8_t*, size_t, const sockaddr*, socklen_t) {

    }
};


constexpr char HOST[] = ":6688";

EchoEvent::KcpListener::Ptr listener;

void signal_handler(int signal) {
    if (signal == SIGINT && listener) {
        listener->stop();
    }
}

int
main(int, char**) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32

    listener = EchoEvent::KcpListener::create(HOST, 2000);

    std::signal(SIGINT, signal_handler);

	listener->run();

    std::printf("EXIT.!!!\n");
#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
