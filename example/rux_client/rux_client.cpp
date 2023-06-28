#include "xq/net/rux_client.hpp"


#define LIMIT (100000)


class EchoService;
static xq::net::Udx<xq::net::RuxClient<EchoService>>::ptr pUdx;

class EchoService {
public:
    void on_message(xq::net::Rux::ptr rux, uint8_t* msg, int msglen) {
        static int count = 0;

        msg[msglen] = 0;
        DLOG("recv: %s\n", (char*)msg);
        if (++count == LIMIT) {
             pUdx->stop();
             DLOG("xmit: %lu£¬ srtt: %d\n", rux->xmit(), rux->srtt());
        }
    }
};


int 
main(int argc, char** argv) {
    char* buf = new char[xq::net::RUX_MSG_MAX]{};
    ASSERT(!rux_env_init());

    std::string endpoint = "1.15.81.179:6688";

    if (argc == 2) {
        endpoint = argv[1];
    }

    EchoService es;
    xq::net::RuxClient client(1, &es);
    xq::net::Udx udx(":0", &client);
    pUdx = &udx;

    client.connect_node(endpoint, sys_time(), udx.snd_que());
    udx.run();

    int64_t beg = sys_time();
    for (int i = 0; i < LIMIT; i++) {
        sprintf(buf, "Hello world: %d", i + 1);;
        client.send(endpoint.c_str(), (uint8_t*)buf, strlen(buf));
    }

    udx.wait();
    DLOG("exit: %ld s !!!", (sys_time() - beg) / 1000000);
    ASSERT(!rux_env_release());

    delete[] buf;
    exit(EXIT_SUCCESS);
}