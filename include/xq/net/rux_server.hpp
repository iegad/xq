#ifndef __XQ_NET_RUX_SERVER__
#define __XQ_NET_RUX_SERVER__


#include <functional>
#include <vector>
#include <thread>
#include "xq/net/rux.hpp"


namespace xq {
namespace net {


class RuxServer {
public:
    RuxServer() 
        : sockfd_(INVALID_SOCKET) {
        int64_t now_us = sys_clock();
        for (int i = 1; i <= RUX_RID_MAX; i++) {
            sessions_.emplace_back(new Rux(i, now_us, output_que_));
        }
    }


    ~RuxServer() {
        if (sockfd_ != INVALID_SOCKET) {
            this->stop();
        }

        for (auto s : sessions_) {
            delete s;
        }
    }


    void run(const char *host, const char *svc) {
        rcv_thread_ = std::thread(std::bind(&RuxServer::_run, this, host, svc));
    }


    void wait() {
        if (rcv_thread_.joinable()) {
            rcv_thread_.join();
        }
    }


    void stop() {
        close(sockfd_);
        sockfd_ = INVALID_SOCKET;
    }


    int join_multicast(const std::string& multi_local_ip, const std::string& multi_route_ip) {
        ASSERT(multi_local_ip.size() > 0 && multi_route_ip.size() > 0);

        int af = xq::net::get_ip_type(multi_route_ip);
        ASSERT(af == AF_INET/* || af == AF_INET6*/);

        ip_mreq mreq;
        ::memset(&mreq, 0, sizeof(mreq));
        if (::inet_pton(af, multi_route_ip.c_str(), &mreq.imr_multiaddr) != 1) {
            return -1;
        }

        if (::inet_pton(af, multi_local_ip.c_str(), &mreq.imr_interface) != 1) {
            return -1;
        }

        if (::setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq))) {
            return -1;
        }

        return 0;
    }


private:
    void _run(const char* host, const char* svc) {
        /* ---------------------------------- 开启服务 ---------------------------------- */
        // Step 1: make udp socket
        sockfd_ = udp_bind(host, svc);
        ASSERT(sockfd_ != INVALID_SOCKET);

        // Step 2: send thread
        snd_thread_ = std::thread(std::bind(&RuxServer::_snd_thread, this));

        // Step 3: rux worker threads
        const int CPUS = sys_cpus();
        for (int i = 0; i < CPUS; i++) {
            auto q = new moodycamel::BlockingConcurrentQueue<PRUX_FRM>();
            job_ques_.emplace_back(q);
            thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
        }

        PRUX_FRM frm = new RUX_FRM;
        Rux* rux;
        int n, qid = 0;

        // Step 4: recv thread
        while (sockfd_ != INVALID_SOCKET) {
            n = recvfrom(sockfd_, (char *)frm->raw, sizeof(frm->raw), 0, (sockaddr *)&frm->name, &frm->namelen);
            if (n < 0) {
                // TODO: err event
                continue;
            }

            if (n > RUX_MTU) {
                // TODO: err
                continue;
            }

            frm->len = n;
            if (frm->check()) {
                continue;
            }

            frm->time_us = sys_clock();
            frm->rux = rux = sessions_[frm->rid - 1];
            if (rux->get_qid() == -1) {
                rux->set_qid(qid++);
                if (qid == CPUS) {
                    qid = 0;
                }
            }

            job_ques_[rux->get_qid()]->enqueue(frm);
            frm = new RUX_FRM;
        }

        /* ---------------------------------- 停止服务 ---------------------------------- */
        delete frm;

        // Step 1: close udp socket
        close(sockfd_);
        sockfd_ = INVALID_SOCKET;

        // Step 2: join send thread
        snd_thread_.join();

        // Step 3: join rux worker threads
        for (auto& wkr : thread_pool_) {
            if (wkr.joinable()) {
                wkr.join();
            }
        }
        thread_pool_.clear();

        for (auto q : job_ques_) {
            delete q;
        }
        
        job_ques_.clear();
    }


    void _rux_thread(moodycamel::BlockingConcurrentQueue<PRUX_FRM> *que) {
        PRUX_FRM frms[128], frm;
        int n, i, res;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];

        while (sockfd_ != INVALID_SOCKET) {
            n = que->wait_dequeue_bulk_timed(frms, 128, 20000);
            for (i = 0; i < n; i++) {
                frm = frms[i];
                res = _rux_handle(frm, msg, RUX_MSG_MAX);
                if (res) {
                    // TODO: remove active session
                }
            }
        } // while(running_);

        // 清理数据
        do {
            n = que->try_dequeue_bulk(frms, 128);
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        } while (n > 0);
    }


    int _rux_handle(PRUX_FRM frm, uint8_t* msg, int msglen) {
        Rux* rux = (Rux*)frm->rux;
        int n;

        if (rux->input(frm)) {
            return -1;
        }

        while (n = rux->recv(msg, msglen), n > 0) {
            msg[n] = 0;
            // TODO: rux msg handle
            std::printf("%s\n", (char*)msg);
        }

        rux->flush(sys_clock());
        return 0;
    }


    void _snd_thread() {
        PRUX_FRM frms[128], frm;
        int n, i;

        while (sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, 128, 50000);
            for (i = 0; i < n; i++) {
                frm = frms[i];
                if (::sendto(sockfd_, (char*)frm->raw, frm->len, 0, (sockaddr*)&frm->name, frm->namelen) != frm->len) {
                    // TODO: error
                }
            }
        }

        // 清理数据
        do {
            n = output_que_.try_dequeue_bulk(frms, 128);
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        } while (n > 0);
    }


    SOCKET sockfd_;
    std::vector<Rux*> sessions_;
    std::vector<uint32_t> active_session_;

    std::thread rcv_thread_;
    std::thread snd_thread_;
    std::vector<std::thread> thread_pool_;
    std::vector<moodycamel::BlockingConcurrentQueue<PRUX_FRM>*> job_ques_;
    moodycamel::BlockingConcurrentQueue<PRUX_FRM> output_que_;


    RuxServer(const RuxServer&) = delete;
    RuxServer(const RuxServer&&) = delete;
    RuxServer& operator=(const RuxServer&) = delete;
    RuxServer& operator=(const RuxServer&&) = delete;
}; // class RuxServer;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_RUX_SERVER__
