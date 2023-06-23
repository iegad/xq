#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/udx.hpp"


constexpr int   NCOUNT = 100;
constexpr char  MCAST[] = "224.0.0.88";   // Udp multi cast address

static int is_server = 0;


class EchoEvent {
public:
    typedef xq::net::Udx<EchoEvent> Udx;


    void on_run(Udx* udx) {
        // ASSERT(!udx->join_multicast(udx->host(), MCAST));
        DLOG("%s:%s is running...\n", udx->host(), udx->svc());
    }


    void on_stop(Udx* udx) {
        DLOG("%s:%s has stopped.\n", udx->host(), udx->svc());
    }

#ifdef _WIN32
    void on_recv(Udx* udx, int err, xq::net::Frame::ptr pfm) {
        if (err) {
            DLOG("recv failed: %d\n", err);
            return;
        }

        pfm->raw[pfm->len] = 0;
        DLOG("recv: %s\n", (char*)pfm->raw);

        if (is_server) {
            ASSERT(!udx->send(pfm));
        }
    }


#else

    void on_recv(Udx* udx, xq::net::Frame::ptr* pfms, int n) {

        xq::net::Frame::ptr pfm;

        for (int i = 0; i < n; i++) {
            pfm = pfms[i];
            if (pfm->len > UDP_MTU) {
                continue;
            }

            pfm->raw[pfm->len] = 0;
            DLOG("recv: %s\n", (char*)pfm->raw);

        }

        if (is_server) {
            udx->send(pfms, n);
        }
    }

#endif

    void on_send(Udx*, int, xq::net::Frame::ptr) {

    }
}; // class EchoEvent


static EchoEvent::Udx::ptr pUdx;
static EchoEvent echo_ev;


static void
server(const std::string& endpoint) {
    EchoEvent::Udx udx(endpoint, &echo_ev);
    is_server = 1;
    pUdx = &udx;
    udx.run(false);
    udx.wait();
}


static void
client(const std::string& endpoint) {
    EchoEvent::Udx udx(":0", &echo_ev);
    pUdx = &udx;
    udx.run();

    for (int i = 0; i < NCOUNT; i++) {
        xq::net::Frame::ptr pfm = new xq::net::Frame;
        pfm->name.ss_family = AF_INET;
        ASSERT(!str2addr(endpoint.c_str(), &pfm->name, &pfm->namelen));

        int n = sprintf((char*)pfm->raw, "Hello world: %d", i);
        pfm->len = n;
        if (udx.send(pfm)) {
            DLOG("send failed\n");
        }
    }

    udx.wait();
}


static void
signal_handler(int sig) {
    if (sig == SIGINT && pUdx) {
        pUdx->stop();
    }
}


int
main(int argc, char** argv) {
    ASSERT(!rux_env_init());

    do {
        if (argc < 2) {
            DLOG("Error: missing arguments !!!\n"
                "Example:\n"
                "  server: ./udx \"127.0.0.1:6688\" 1\n"
                "  client: ./udx \"127.0.0.1:6688\"\n");
            break;
        }

        ASSERT(std::signal(SIGINT, signal_handler) != SIG_ERR);

        if (argc > 2) {
            DLOG("server is running...\n");
            server(argv[1]);
        }
        else {
            DLOG("client is running...\n");
            client(argv[1]);
        }
    } while (0);

    ASSERT(!rux_env_release());
    DLOG("Exit !!!\n");
    exit(0);
}
