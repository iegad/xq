#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif
#include <csignal>
#include "xq/net/udx.hpp"


static std::string endpoint;
static bool is_server = false;
static xq::net::Udx *pUdx;

constexpr char MCAST[] = "224.0.0.88";   // Udp multi cast address


static void
on_recv_frame(xq::net::Udx *udx, int err, xq::net::Frame::ptr pfm) {
    if (err != 0) {
        DLOG("recvfrom failed: : %d\n", err);
        return;
    }

    pfm->raw[pfm->len] = 0;
    DLOG("%s\n", (char*)pfm->raw);
}


static void
on_prev_run(xq::net::Udx* udx) {
    // UDP multicast
    // ASSERT(!udx->join_multicast(udx->host(), MCAST));
}


static void
server(const std::string &endpoint) {
    xq::net::UdxOption opt;
    
    opt.endpoint            = endpoint;
    opt.prev_run_handler    = on_prev_run;
    opt.recv_frame_handler  = on_recv_frame;

    xq::net::Udx udx(&opt);
    pUdx = &udx;
    udx.run();
    udx.wait();
}


static void
client(const std::string &endpoint) {
    xq::net::UdxOption opt;
    opt.endpoint = ":0";
    xq::net::Udx udx(&opt);
    pUdx = &udx;
    udx.run();

    for (int i = 0; i < 100; i++) {
        xq::net::Frame::ptr pfm = xq::net::Frame::get();
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
            is_server = true;
            DLOG("server is running...\n");
            server(argv[1]);
        } else {
            DLOG("client is running...\n");
            client(argv[1]);
        }
    } while(0);

    ASSERT(!rux_env_release());
    DLOG("Exit !!!\n");
    exit(0);
}
