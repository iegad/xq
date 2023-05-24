#ifndef __XQ_NET_RUX_SERVER__
#define __XQ_NET_RUX_SERVER__


#include <functional>
#include <vector>
#include <thread>
#include <unordered_set>
#include "xq/net/rux.hpp"


namespace xq {
namespace net {


// ############################################################################################################
// Rux 服务端
// ############################################################################################################
class RuxServer {
public:


    // ========================================================================================================
    // 构造函数.
    // ========================================================================================================
    RuxServer() 
        : sockfd_(INVALID_SOCKET)
        , start_us_(sys_clock()) {

        // new RUX_RID_MAX Rux Sessions
        for (int i = 1; i <= RUX_RID_MAX; i++) {
            sessions_.emplace_back(new Rux(i, start_us_, output_que_));
        }
    }


    // ========================================================================================================
    // 析构函数
    // ========================================================================================================
    ~RuxServer() {
        if (sockfd_ != INVALID_SOCKET) {
            this->stop();
        }

        // delete RUX_RID_MAX Rux Sessions
        for (auto s : sessions_) {
            delete s;
        }
    }


    // ========================================================================================================
    // 启动服务
    //      该方法分为同步和异步启动.
    //      异步启动需要通过 wait方法等待 run 的结束
    // -------------------------------
    // @host:   IP/domain
    // @svc:    port/service
    // @async:  是否异步启动
    // ========================================================================================================
    inline void run(const char *host, const char *svc, bool async = true) {
        if (async) {
            rcv_thread_ = std::thread(std::bind(&RuxServer::_rcv_thread, this, host, svc));
        }
        else {
            _rcv_thread(host, svc);
        }
    }


    // ========================================================================================================
    // 等待异步服务结束
    // ========================================================================================================
    void wait() {
        if (rcv_thread_.joinable()) {
            rcv_thread_.join();
        }
    }


    // ========================================================================================================
    // 停止服务
    // ========================================================================================================
    void stop() {
        close(sockfd_);
        sockfd_ = INVALID_SOCKET;
    }


    // ========================================================================================================
    // 加入组播, 预留功能. 
    //      目前可支持IPV4组播
    // -------------------------------
    // @multi_local_ip: 组播地址
    // @multi_route_ip: 本机路由地址
    // ========================================================================================================
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
    // ========================================================================================================
    // input 线程
    // ========================================================================================================
    void _rcv_thread(const char* host, const char* svc) {
        ASSERT(host, svc);

        /* ---------------------------------- 开启服务 ---------------------------------- */

        // Step 1: make udp socket
        sockfd_ = udp_bind(host, svc);
        ASSERT(sockfd_ != INVALID_SOCKET);

        // Step 2: 启动 output线程
        snd_thread_ = std::thread(std::bind(&RuxServer::_snd_thread, this));

        // Step 3: 启动 rux update 线程
        upd_thread_ = std::thread(std::bind(&RuxServer::_update_thread, this));

        // Step 4: 启动 rux 协议 线程
        //      启动线程的同时, 为每个 rux 线程分配 帧工作队列
        const int CPUS = sys_cpus();
        for (int i = 0; i < CPUS; i++) {
            auto q = new moodycamel::BlockingConcurrentQueue<PRUX_FRM>();
            frm_ques_.insert(std::make_pair(i, q));
            rux_thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
        }

        // Step 5: loop recvfrom
        PRUX_FRM    frm = new RUX_FRM;
        int         qid = 0;            // rux queue id, RoundRobin方式
        int         n;
        Rux*        rux;

        while (sockfd_ != INVALID_SOCKET) {
            n = recvfrom(sockfd_, (char *)frm->raw, sizeof(frm->raw), 0, (sockaddr *)&frm->name, &frm->namelen);
            if (n < 0) {
                // IO recv error
                // TODO: err event
                continue;
            }

            if (n > RUX_MTU || n < RUX_FRM_HDR_SIZE + RUX_SEG_HDR_SIZE) {
                // rux frame's length error
                // TODO: err
                continue;
            }
            
            frm->len = n;
            if (frm->check()) {
                // rux frame error
                continue;
            }

            frm->time_us = sys_clock();
            frm->rux = rux = sessions_[frm->rid - 1];

            if (rux->qid() == -1) {
                sess_lkr_.lock();
                active_session_.insert(rux->rid());
                sess_lkr_.unlock();
                rux->set_qid(qid++);
                if (qid == frm_ques_.size()) {
                    qid = 0;
                }
            }

            // move frm to frm queue
            frm_ques_[rux->qid()]->enqueue(frm);
            frm = new RUX_FRM;
        }

        /* ---------------------------------- 停止服务 ---------------------------------- */
        delete frm;

        // Step 1: close udp socket
        close(sockfd_);
        sockfd_ = INVALID_SOCKET;

        // Step 2: join send thread
        snd_thread_.join();
        
        // Step 3: join rux update thread
        upd_thread_.join();

        // Step 4: join rux worker threads
        while (frm_ques_.size() > 0) {
            auto itr = rux_thread_pool_.begin();
            itr->join();
            rux_thread_pool_.erase(itr);
        }

        // Step 5: delete all frames queues
        while (frm_ques_.size() > 0) {
            auto itr = frm_ques_.begin();
            delete itr->second;
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
                delete frm;
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
            frm_ques_.insert(std::make_pair(i, q));
            rux_thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
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
                rux_qid = rux->qid();
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
                    frm_ques_[i]->enqueue_bulk(wkr_frms[i], nwkr_frms[i]);
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
        while (n = output_que_.try_dequeue_bulk(frms, SNDMMSG_SIZE), n > 0) {
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        }
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

                    while (nmsg = rux->recv(msg), nmsg > 0) {
                        // TODO: event handle
                        msg[nmsg] = 0;
                        DLOG("%s\n", (char*)msg);
                    }

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


    void _update_thread() {
        uint32_t rid;
        uint64_t now_us;

        std::list<uint32_t>::iterator  itr;
        std::list<uint32_t>                     sessions;

        while (sockfd_ != INVALID_SOCKET) {
            now_us = sys_clock();
            sess_lkr_.lock();
            if (active_session_.size() > 0) {
                std::copy(active_session_.begin(), active_session_.end(), std::back_inserter(sessions));
            }
            sess_lkr_.unlock();

            itr = sessions.begin();
            while (itr != sessions.end()) {
                rid = *itr;
                if (sessions_[rid - 1]->output(now_us) < 0) {
                    sess_lkr_.lock();
                    active_session_.erase(rid);
                    sess_lkr_.unlock();
                    continue;
                }
                ++itr;
            }

            std::this_thread::yield();
        }
    }


    SOCKET                                                                  sockfd_;            // 服务端 UDP 套接字
    uint64_t                                                                start_us_;

    std::thread                                                             rcv_thread_;        // input 线程
    std::thread                                                             snd_thread_;        // output 线程
    std::thread                                                             upd_thread_;        // rux update 线程
    std::list<std::thread>                                                  rux_thread_pool_;   // rux 线程

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>                           output_que_;        // output 队列; 
    std::unordered_map<int, moodycamel::BlockingConcurrentQueue<PRUX_FRM>*> frm_ques_;          // 帧工作队列;  input 线程为生产者, rux 线程为消费者
    
    SPIN_LOCK                                                               sess_lkr_;
    std::vector<Rux*>                                                       sessions_;
    std::unordered_set<uint32_t>                                            active_session_;


    RuxServer(const RuxServer&) = delete;
    RuxServer(const RuxServer&&) = delete;
    RuxServer& operator=(const RuxServer&) = delete;
    RuxServer& operator=(const RuxServer&&) = delete;
}; // class RuxServer;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_RUX_SERVER__
