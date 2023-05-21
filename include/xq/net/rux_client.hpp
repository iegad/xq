#ifndef __XQ_NET_RUX_CLIENT__
#define __XQ_NET_RUX_CLIENT__


#include <string>
#include <unordered_map>
#include <functional>
#include <xq/third/blockingconcurrentqueue.h>
#include "xq/net/rux.hpp"


namespace xq {
namespace net {


class RuxClient {
public:
    RuxClient(uint32_t rid)
        : sockfd_(INVALID_SOCKET)
        , rid_(rid)
        , output_que_()
    {}


    void run() {
        rcv_thread_ = std::thread(std::bind(&RuxClient::_rcv_thread, this));
    }


    void stop() {
        if (sockfd_ != INVALID_SOCKET) {
            close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    void wait() {
        if (rcv_thread_.joinable()) {
            rcv_thread_.join();
        }
    }


    void add_node(const char* endpoint) {
        uint64_t now_us = sys_clock();
        Rux* rux = new Rux(rid_, now_us, output_que_);
        rux->addr()->ss_family = AF_INET;
        ASSERT(!str2addr(endpoint, rux->addr(), rux->addrlen()));
        rux->active(now_us);
        rux_map_.insert(std::make_pair(endpoint, rux));
    }


    int send(const char* endpoint, const uint8_t* msg, uint16_t msglen) {
        auto itr = rux_map_.find(endpoint);
        if (itr == rux_map_.end()) {
            return -1;
        }

        return itr->second->send(msg, msglen);
    }


    int output(uint64_t now_us) {
        int n = 0, res;
        for (auto& itr : rux_map_) {
            res = itr.second->output(now_us);
            if (res < 0) {
                return -1;
            }
            n += res;
        }

        return n;
    }


private:
    void _rcv_thread() {
        sockfd_ = udp_bind("0.0.0.0", "0");
        ASSERT(sockfd_ != INVALID_SOCKET);

        snd_thread_ = std::thread(std::bind(&RuxClient::_snd_thread, this));

        PRUX_FRM frm = new RUX_FRM;
        int n;
        char endpoint[INET6_ADDRSTRLEN + 7];
        Rux* rux;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];

        while (sockfd_ != INVALID_SOCKET) {
            n = recvfrom(sockfd_, (char*)frm->raw, sizeof(frm->raw), 0, (sockaddr*)&frm->name, &frm->namelen);
            if (n < 0) {
                // TODO: error
                continue;
            }

            if (n > RUX_MTU) {
                // TODO: error
                continue;
            }

            frm->len = n;
            if (frm->check()) {
                // TODO: error
                continue;
            }

            frm->time_us = sys_clock();
            ::memset(endpoint, 0, sizeof(endpoint));
            ASSERT(!addr2str(&frm->name, endpoint, INET6_ADDRSTRLEN + 7));
            auto itr = rux_map_.find(endpoint);
            if (itr == rux_map_.end()) {
                continue;
            }

            rux = itr->second;
            if (rux->input(frm)) {
                continue;
            }

            while (n = rux->recv(msg, RUX_MSG_MAX), n > 0) {
                char* hex = new char[n * 2 + 1];
                n = bin2hex(msg, n, hex, n * 2);
                hex[n] = 0;
            }
        }

        snd_thread_.join();
        delete frm;
        delete[] msg;
    }


    void _snd_thread() {
        constexpr int FRMS_MAX = 64;

        PRUX_FRM frms[FRMS_MAX], frm;
        int n, i;
        while (sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, FRMS_MAX, 50000);
            for (i = 0; i < n; i++) {
                frm = frms[i];
                if (::sendto(sockfd_, (char*)frm->raw, frm->len, 0, (sockaddr*)&frm->name, frm->namelen) < 0) {
                    // TODO: ...
                    DLOG("sendto failed: %d\n", errcode);
                }
            }
        }

        while (n = output_que_.try_dequeue_bulk(frms, FRMS_MAX), n > 0) {
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        }
    }


    SOCKET sockfd_;
    uint32_t rid_;

    std::thread rcv_thread_;
    std::thread snd_thread_;

    std::unordered_map<std::string, Rux*> rux_map_;
    moodycamel::BlockingConcurrentQueue<PRUX_FRM> output_que_;


    RuxClient(const RuxClient&) = delete;
    RuxClient(const RuxClient&&) = delete;
    RuxClient& operator=(const RuxClient&) = delete;
    RuxClient& operator=(const RuxClient&&) = delete;
}; // class RuxClient;


} // namespace net
} // namespace xq


#endif // __XQ_NET_RUX_CLIENT__
