#define KL_EVENT_ON_START        1
#define KL_EVENT_ON_STOP         1
#define KL_EVENT_ON_CONNECTED    1
#define KL_EVENT_ON_RECONNECTED  1
#define KL_EVENT_ON_DISCONNECTED 1
#define KL_EVENT_ON_SEND         1
#define KL_EVENT_ON_RECV         1


#include "xq/net//kcp_listener.hpp"
#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

#include <csignal>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>


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

    int on_recv(const uint8_t *raw, size_t rawlen, const sockaddr*, socklen_t) {
        xq::net::Kcp::Segment* seg = xq::net::Kcp::Segment::pool()->get();
        const uint8_t* p = raw;

        while (rawlen > 0) {
            int n = xq::net::Kcp::decode(p, rawlen, seg);
            raw += n;
            rawlen -= n;
            rl_->info("RCV-> " + seg->to_string());
        }

        xq::net::Kcp::Segment::pool()->put(seg);
        return 0;
    }

    void on_send(const uint8_t* raw, size_t rawlen, const sockaddr*, socklen_t) {
        xq::net::Kcp::Segment* seg = xq::net::Kcp::Segment::pool()->get();
        const uint8_t* p = raw;

        while (rawlen > 0) {
            int n = xq::net::Kcp::decode(p, rawlen, seg);
            raw += n;
            rawlen -= n;
            tl_->info("SND-> " + seg->to_string());
        }

        xq::net::Kcp::Segment::pool()->put(seg);
    }


    EchoEvent()
        : rl_(spdlog::rotating_logger_st("recv", SPDLOG_FILENAME_T("recv.txt"), 1024 * 1024 * 1024, 0))
        , tl_(spdlog::rotating_logger_st("send", SPDLOG_FILENAME_T("send.txt"), 1024 * 1024 * 1024, 0))
    {}

    ~EchoEvent() {
        rl_->flush();
    }

private:
    std::shared_ptr<spdlog::logger> rl_;
    std::shared_ptr<spdlog::logger> tl_;
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
