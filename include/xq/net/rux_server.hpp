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
        rcv_thread_ = std::thread(std::bind(&RuxServer::_rcv_thread, this, host, svc));
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
#ifdef WIN32
    void _rcv_thread(const char* host, const char* svc) {
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
            frm_ques_.emplace_back(q);
            thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
        }

        PRUX_FRM frm = new RUX_FRM;
        Rux* rux;
        int n;

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

            frm_ques_[frm->rid % frm_ques_.size()]->enqueue(frm);
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

        while (frm_ques_.size() > 0) {
            auto itr = frm_ques_.begin();
            delete *itr;
            frm_ques_.erase(itr);
        }
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
#else
    void _rcv_thread(const char *host, const char *svc) {
        constexpr int RCVMMSG_SIZE = 128;
        constexpr static timeval TIMEOUT{0, 500000};

        sockfd_ = udp_bind(host, svc);
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        snd_thread_ = std::thread(std::bind(&RuxServer::_snd_thread, this));

        const int CPUS = sys_cpus();
        int i, n = RCVMMSG_SIZE, err, qid = 0, rux_qid;
        uint64_t now_us;
        PRUX_FRM** wkr_frms = new PRUX_FRM*[CPUS];
        int* nwkr_frms = new int[CPUS];

        for (i = 0; i < CPUS; i++) {
            auto q = new moodycamel::BlockingConcurrentQueue<PRUX_FRM>;
            job_ques_.emplace_back(q);
            thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
            wkr_frms[i] = new RUX_FRM*[RCVMMSG_SIZE];
            nwkr_frms[i] = 0;
        }

        mmsghdr msgs[RCVMMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));
        msghdr *hdr;

        iovec iovecs[RCVMMSG_SIZE];
        PRUX_FRM frms[RCVMMSG_SIZE] = {nullptr}, frm;
        Rux* rux;

        for (i = 0; i < n; i++) {
            frm = frms[i] = new RUX_FRM;
            hdr = &msgs[i].msg_hdr;
            hdr->msg_name = &frm->name;
            hdr->msg_namelen = frm->namelen;
            hdr->msg_iov = &iovecs[i];
            hdr->msg_iovlen = 1;
            iovecs[i].iov_base = frm->raw;
            iovecs[i].iov_len = sizeof(frm->raw);
        }

        while(sockfd_ != INVALID_SOCKET) {
            n = ::recvmmsg(sockfd_, msgs, RCVMMSG_SIZE, MSG_WAITFORONE, nullptr);
            if (n < 0) {
                err = errcode;
                if (err != EAGAIN && err != EINTR) {
                    // TODO: error
                    break;
                }
                continue;
            }

            if (n == 0) {
                continue;
            }

            now_us = sys_clock();
            for (i = 0; i < n; i++) {
                if (msgs[i].msg_len > RUX_MTU) {
                    // TODO: error
                    continue;
                }

                frm = frms[i];
                frm->len = msgs[i].msg_len;
                if (frm->check()) {
                    // TODO: error
                    continue;
                }

                frm->time_us = now_us;
                frm->rux = rux = sessions_[frm->rid - 1];
                rux_qid = rux->get_qid();
                if (rux_qid == -1) {
                    rux->set_qid(qid);
                    rux_qid = qid;
                    if (++qid == CPUS) {
                        qid = 0;
                    }
                }

                wkr_frms[rux_qid][nwkr_frms[rux_qid]++] = frm;
                frm = frms[i] = new RUX_FRM;
                hdr = &msgs[i].msg_hdr;
                hdr->msg_name = &frm->name;
                hdr->msg_namelen = frm->namelen;
                hdr->msg_iov = &iovecs[i];
                hdr->msg_iovlen = 1;
                iovecs[i].iov_base = frm->raw;
                iovecs[i].iov_len = sizeof(frm->raw);
            }

            for (i = 0; i < CPUS; i++) {
                if (nwkr_frms[i] > 0) {
                    job_ques_[i]->enqueue_bulk(wkr_frms[i], nwkr_frms[i]);
                    nwkr_frms[i] = 0;
                }
            }
        } // while(sockfd_ != INVALID_SOCKET;

        for (i = 0; i < CPUS; i++) {
            delete[] wkr_frms[i];
        }

        delete[] nwkr_frms;
        for (i = 0; i < RCVMMSG_SIZE; i++) {
            delete frms[i];
        }
    }


    void _snd_thread() {
        constexpr int SNDMMSG_SIZE = 128;
        mmsghdr msgs[SNDMMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[SNDMMSG_SIZE];
        msghdr *hdr;
        PRUX_FRM frm, frms[SNDMMSG_SIZE];

        int res = 0, i, n;

        while(sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, SNDMMSG_SIZE, 50000);
            if (n > 0) {
                for (i = 0; i < n; i++) {
                    frm = frms[i];
                    hdr = &msgs[i].msg_hdr;
                    hdr->msg_name = &frm->name;
                    hdr->msg_namelen = frm->namelen;
                    hdr->msg_iov = &iovecs[i];
                    hdr->msg_iovlen = 1;
                    iovecs[i].iov_base = frm->raw;
                    iovecs[i].iov_len = frm->len;
                }

                res = ::sendmmsg(sockfd_, msgs, n, 0);
                for (i = 0; i < n; i++) {
                    delete frms[i];
                }
                if (res < 0) {
                    // TODO: error
                }
            }
        }

        // 清理数据
        do {
            n = output_que_.try_dequeue_bulk(frms, SNDMMSG_SIZE);
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        } while (n > 0);
    }
#endif


    void _rux_thread(moodycamel::BlockingConcurrentQueue<PRUX_FRM> *que) {
        constexpr int FRMS_MAX = 128;
        constexpr int QUE_TIMEOUT = 100000;

        PRUX_FRM frms[FRMS_MAX], frm;
        int nfrms, i, nmsg;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];
        Rux* rux;

        while (sockfd_ != INVALID_SOCKET) {
            nfrms = que->wait_dequeue_bulk_timed(frms, FRMS_MAX, QUE_TIMEOUT);
            if (nfrms > 0) {
                for (i = 0; i < nfrms; i++) {
                    frm = frms[i];
                    rux = (Rux*)frm->rux;
                    if (rux->input(frm)) {
                        // TODO: remove active session
                        delete frm;
                        break;
                    }

                    while (nmsg = rux->recv(msg, RUX_MSG_MAX), nmsg > 0) {
                        // TODO: event handle
                        msg[nmsg] = 0;
                        DLOG("%s\n", (char*)msg);
                    }

                    rux->output(frm->time_us);
                    delete frm;
                }
            }
        } // while(sockfd_ != INVALID_SOCKET);

        // 清理数据
        while (nfrms = output_que_.try_dequeue_bulk(frms, FRMS_MAX), nfrms > 0) {
            for (i = 0; i < nfrms; i++) {
                delete frms[i];
            }
        }

        delete[] msg;
    }


    SOCKET sockfd_;
    std::vector<Rux*> sessions_;
    std::vector<uint32_t> active_session_;

    std::thread rcv_thread_;
    std::thread snd_thread_;
    std::vector<std::thread> thread_pool_;
    std::vector<moodycamel::BlockingConcurrentQueue<PRUX_FRM>*> frm_ques_;
    moodycamel::BlockingConcurrentQueue<PRUX_FRM> output_que_;


    RuxServer(const RuxServer&) = delete;
    RuxServer(const RuxServer&&) = delete;
    RuxServer& operator=(const RuxServer&) = delete;
    RuxServer& operator=(const RuxServer&&) = delete;
}; // class RuxServer;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_RUX_SERVER__
