#include "xq/net/rux_client.hpp"


class EchoEvent {
public:
    void on_error(xq::net::ErrType err_type, void* arg) {

    }

    void on_connected(xq::net::Rux* rux) {
        
    }

    void on_disconnected(xq::net::Rux* rux) {

    }

    void on_message(xq::net::Rux* rux, const uint8_t* msg, int msglen) {

    }
};


xq::net::RuxClient<EchoEvent>* client;

 //#define SERVER_ENDPOINT ("192.168.0.104:6688")
#define SERVER_ENDPOINT ("127.0.0.1:6688")
//#define SERVER_ENDPOINT ("1.15.81.179:6688")


void
snd_worker() {
    char* buf = new char[xq::net::RUX_MSG_MAX]{};

    for (int i = 0; i < 10000; i++) {
        sprintf(buf, "Hello world: %d", i + 1);
        while (client->send(SERVER_ENDPOINT, (uint8_t*)buf, strlen(buf))) {
            _mm_pause();
        }
    }

    delete[] buf;
}



int 
main(int argc, char** argv) {
    ASSERT(!rux_env_init());
    client = new xq::net::RuxClient<EchoEvent>(1);
    client->connect_node(SERVER_ENDPOINT, sys_clock());
    std::thread t(snd_worker);
    client->run();
    t.join();
    client->wait();

    /*int n;

    SOCKET sockfd = udp_bind("0.0.0.0", "0");
    ASSERT(sockfd != INVALID_SOCKET);

    xq::net::PRUX_FRM f1 = new xq::net::RUX_FRM;
    f1->rid = 1;
    f1->wnd = 128;
    f1->name.ss_family = AF_INET;
    str2addr("127.0.0.1:6688", &f1->name, &f1->namelen);
    f1->raw + xq::net::RUX_FRM_HDR_SIZE;
    xq::net::PRUX_SEG s1 = new xq::net::RUX_SEG;
    s1->cmd = xq::net::RUX_CMD_CON;
    s1->sn = 0;
    s1->us = 0;
    s1->frg = 4;
    s1->set_data((uint8_t*)"Hello world: 1", 14);
    f1->len = s1->encode(f1->raw + xq::net::RUX_FRM_HDR_SIZE, xq::net::RUX_MTU - xq::net::RUX_FRM_HDR_SIZE) + xq::net::RUX_FRM_HDR_SIZE;
    f1->setup();

    xq::net::PRUX_FRM f2 = new xq::net::RUX_FRM;
    f2->rid = 1;
    f2->wnd = 128;
    f2->name.ss_family = AF_INET;
    str2addr("127.0.0.1:6688", &f2->name, &f2->namelen);
    f2->raw + xq::net::RUX_FRM_HDR_SIZE;
    xq::net::PRUX_SEG s2 = new xq::net::RUX_SEG;
    s2->cmd = xq::net::RUX_CMD_PSH;
    s2->sn = 1;
    s2->us = 1;
    s2->frg = 3;
    s2->set_data((uint8_t*)"Hello world: 2", 14);
    f2->len = s2->encode(f2->raw + xq::net::RUX_FRM_HDR_SIZE, xq::net::RUX_MTU - xq::net::RUX_FRM_HDR_SIZE) + xq::net::RUX_FRM_HDR_SIZE;
    f2->setup();

    xq::net::PRUX_FRM f3 = new xq::net::RUX_FRM;
    f3->rid = 1;
    f3->wnd = 128;
    f3->name.ss_family = AF_INET;
    str2addr("127.0.0.1:6688", &f3->name, &f3->namelen);
    f3->raw + xq::net::RUX_FRM_HDR_SIZE;
    xq::net::PRUX_SEG s3 = new xq::net::RUX_SEG;
    s3->cmd = xq::net::RUX_CMD_PSH;
    s3->sn = 2;
    s3->us = 2;
    s3->frg = 2;
    s3->set_data((uint8_t*)"Hello world: 3", 14);
    f3->len = s3->encode(f3->raw + xq::net::RUX_FRM_HDR_SIZE, xq::net::RUX_MTU - xq::net::RUX_FRM_HDR_SIZE) + xq::net::RUX_FRM_HDR_SIZE;
    f3->setup();

    xq::net::PRUX_FRM f4 = new xq::net::RUX_FRM;
    f4->rid = 1;
    f4->wnd = 128;
    f4->name.ss_family = AF_INET;
    str2addr("127.0.0.1:6688", &f4->name, &f4->namelen);
    f4->raw + xq::net::RUX_FRM_HDR_SIZE;
    xq::net::PRUX_SEG s4 = new xq::net::RUX_SEG;
    s4->cmd = xq::net::RUX_CMD_PSH;
    s4->sn = 3;
    s4->us = 3;
    s4->frg = 1;
    s4->set_data((uint8_t*)"Hello world: 4", 14);
    f4->len = s4->encode(f4->raw + xq::net::RUX_FRM_HDR_SIZE, xq::net::RUX_MTU - xq::net::RUX_FRM_HDR_SIZE) + xq::net::RUX_FRM_HDR_SIZE;
    f4->setup();

    xq::net::PRUX_FRM f5 = new xq::net::RUX_FRM;
    f5->rid = 1;
    f5->wnd = 128;
    f5->name.ss_family = AF_INET;
    str2addr("127.0.0.1:6688", &f5->name, &f5->namelen);
    f5->raw + xq::net::RUX_FRM_HDR_SIZE;
    xq::net::PRUX_SEG s5 = new xq::net::RUX_SEG;
    s5->cmd = xq::net::RUX_CMD_PSH;
    s5->sn = 4;
    s5->us = 4;
    s5->frg = 0;
    s5->set_data((uint8_t*)"Hello world: 5", 14);
    f5->len = s5->encode(f5->raw + xq::net::RUX_FRM_HDR_SIZE, xq::net::RUX_MTU - xq::net::RUX_FRM_HDR_SIZE) + xq::net::RUX_FRM_HDR_SIZE;
    f5->setup();

    n = ::sendto(sockfd, (char*)f1->raw, f1->len, 0, (sockaddr*)&f1->name, f1->namelen);
    ASSERT(n == f1->len);
    n = ::sendto(sockfd, (char*)f5->raw, f5->len, 0, (sockaddr*)&f5->name, f5->namelen);
    ASSERT(n == f5->len);
    n = ::sendto(sockfd, (char*)f3->raw, f3->len, 0, (sockaddr*)&f3->name, f3->namelen);
    ASSERT(n == f3->len);
    n = ::sendto(sockfd, (char*)f2->raw, f2->len, 0, (sockaddr*)&f2->name, f2->namelen);
    ASSERT(n == f2->len);
    n = ::sendto(sockfd, (char*)f4->raw, f4->len, 0, (sockaddr*)&f4->name, f4->namelen);
    ASSERT(n == f4->len);


    close(sockfd);*/
    ASSERT(!rux_env_release());
    exit(EXIT_SUCCESS);
}