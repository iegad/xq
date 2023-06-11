#ifndef __XQ_NET_RUX_SERVER__
#define __XQ_NET_RUX_SERVER__


#include <functional>
#include <vector>
#include <thread>
#include <unordered_set>
#include "xq/net/rux.hpp"
#include "xq/net/timer.hpp"


namespace xq {
namespace net {


// ############################################################################################################
// Rux 服务端
// ############################################################################################################
template<class TEvent>
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
            sessions_.emplace_back(new Rux(i, start_us_, output_que_, &update_que_, &ts_));
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
        if (!async) {
            _rcv_thread(host, svc);
            return;
        }
        
        rcv_thread_ = std::thread(std::bind(&RuxServer::_rcv_thread, this, host, svc));
    }


    // ========================================================================================================
    // 停止服务
    // ========================================================================================================
    void stop() {
        if (sockfd_ != INVALID_SOCKET) {
            close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }

        ts_.stop();
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
        ASSERT(host && svc);

        /* ---------------------------------- 开启服务 ---------------------------------- */

        // Step 1: make udp socket
        sockfd_ = udp_bind(host, svc);
        ASSERT(sockfd_ != INVALID_SOCKET);

        // Step 2: 启动 output线程
        snd_thread_ = std::thread(std::bind(&RuxServer::_snd_thread, this));

        // Step 3: 启动 rux update 线程
        ts_.run();
        upd_thread_ = std::thread(std::bind(&RuxServer::_update_thread, this));

        // Step 4: 启动 rux 协议 线程
        //      启动线程的同时, 为每个 rux 线程分配 帧工作队列
        int nprocess = sys_cpus() - 1;
        if (nprocess == 0) {
            nprocess = 1;
        }
        for (int i = 0; i < nprocess; i++) {
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
                ev_.on_error(ErrType::IO_RCV, (void*)((int64_t)errcode));
                continue;
            }

            frm->len = n;
            if (n > RUX_MTU || n < RUX_FRM_HDR_SIZE || frm->check()) {
                // rux IO input error
                ev_.on_error(ErrType::IO_RCV_FRAME, frm);
                continue;
            }

            frm->time_us = sys_clock();
            frm->rux = rux = sessions_[frm->rid - 1];

            if (rux->state()) {
                rux->set_qid(qid++);
                if (qid == frm_ques_.size()) {
                    qid = 0;
                }

                if (ev_.on_connected(rux)) {
                    continue;
                }
            }

            rux->set_remote_addr(&frm->name, frm->namelen);
            // move frm to frm queue
            frm_ques_[rux->qid()]->enqueue(frm);
            frm = new RUX_FRM;
        }

        /* ---------------------------------- 停止服务 ---------------------------------- */
        delete frm;

        // Step 1: join send thread
        snd_thread_.join();
        
        // Step 2: join rux update thread
        ts_.wait();
        upd_thread_.join();

        // Step 3: join rux worker threads
        while (frm_ques_.size() > 0) {
            auto itr = rux_thread_pool_.begin();
            itr->join();
            rux_thread_pool_.erase(itr);
        }

        // Step 4: delete all frames queues
        while (frm_ques_.size() > 0) {
            auto itr = frm_ques_.begin();
            delete itr->second;
            frm_ques_.erase(itr);
        }

        for (auto& r : sessions_) {
            r->set_qid(-1);
            r->set_state(-1);
        }
    }


    void _snd_thread() {
        PRUX_FRM frms[128], frm;
        int n, i;

        while (sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, 128, 200 * 1000); // wait 200 milliseconds
            for (i = 0; i < n; i++) {
                frm = frms[i];
                if (::sendto(sockfd_, (char*)frm->raw, frm->len, 0, (sockaddr*)&frm->name, frm->namelen) != frm->len) {
                    ev_.on_error(ErrType::IO_SND, (void*)((int64_t)errcode));
                }
                delete frm;
            }
        }

        // 清理数据
        while (n = output_que_.try_dequeue_bulk(frms, 128), n > 0) {
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        } 
    }


#else


    void _rcv_thread(const char *host, const char *svc) {
        constexpr int RCVMMSG_SIZE = 128;
        constexpr timeval TIMEOUT = { .tv_sec = 0, .tv_usec = 200 * 1000};

        // Step 1: init socket
        sockfd_ = udp_bind(host, svc);
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        // Step 2: start IO output thread
        snd_thread_ = std::thread(std::bind(&RuxServer::_snd_thread, this));

        // Step 3: start rux update thread
        upd_thread_ = std::thread(std::bind(&RuxServer::_update_thread, this));

        // Step 4: init frame's queues and start rux protocol work threads.
        int nprocess = sys_cpus() - 1;
        if (nprocess == 0) {
            nprocess = 1;
        }

        int i, n = RCVMMSG_SIZE, err, qid = 0;
        uint64_t now_us;

        PRUX_FRM** rux_frms = new PRUX_FRM*[nprocess];  //
        int* rux_frms_count = new int[nprocess]{};      //

        for (i = 0; i < nprocess; i++) {
            auto q = new moodycamel::BlockingConcurrentQueue<PRUX_FRM>;
            frm_ques_.insert(std::make_pair(i, q));
            rux_thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, q)));
            rux_frms[i] = new RUX_FRM*[RCVMMSG_SIZE];
        }

        mmsghdr msgs[RCVMMSG_SIZE];
        msghdr* hdr;
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[RCVMMSG_SIZE];
        PRUX_FRM frms[RCVMMSG_SIZE] = {nullptr}, frm;
        Rux* rux;

        for (i = 0; i < n; i++) {
            frm = frms[i] = new RUX_FRM;

            hdr                 = &msgs[i].msg_hdr;
            hdr->msg_name       = &frm->name;
            hdr->msg_namelen    = frm->namelen;
            hdr->msg_iov        = &iovecs[i];
            hdr->msg_iovlen     = 1;
            iovecs[i].iov_base  = frm->raw;
            iovecs[i].iov_len   = sizeof(frm->raw);
        }

        while(sockfd_ != INVALID_SOCKET) {
            n = ::recvmmsg(sockfd_, msgs, RCVMMSG_SIZE, MSG_WAITFORONE, nullptr);
            if (n < 0) {
                err = errcode;
                if (err != EAGAIN && err != EINTR) {
                    ev_.on_error(ErrType::IO_RCV, (void*)((int64_t)err));
                    break;
                }
                continue;
            }

            if (n == 0) {
                continue;
            }

            now_us = sys_clock();
            for (i = 0; i < n; i++) {
                frm = frms[i];
                frm->len = msgs[i].msg_len;

                if (frm->len > RUX_MTU || frm->len < RUX_FRM_HDR_SIZE || frm->check()) {
                    ev_.on_error(ErrType::IO_RCV_FRAME, frm);
                    continue;
                }

                frm->time_us = now_us;
                frm->rux = rux = sessions_[frm->rid - 1];

                if (rux->state()) {
                    rux->set_qid(qid++);
                    if (qid == nprocess) {
                        qid = 0;
                    }

                    if (ev_.on_connected(rux)) {
                        continue;
                    }

                    sess_lkr_.lock();
                    active_session_.insert(rux->rid());
                    sess_lkr_.unlock();
                }

                rux->set_remote_addr(&frm->name, frm->namelen);
                rux_frms[rux->qid()][rux_frms_count[rux->qid()]++] = frm;

                //
                frm = frms[i] = new RUX_FRM;

                hdr                 = &msgs[i].msg_hdr;
                hdr->msg_name       = &frm->name;
                hdr->msg_namelen    = frm->namelen;
                hdr->msg_iov        = &iovecs[i];
                hdr->msg_iovlen     = 1;
                iovecs[i].iov_base  = frm->raw;
                iovecs[i].iov_len   = sizeof(frm->raw);
            }

            for (i = 0; i < nprocess; i++) {
                if (rux_frms_count[i] > 0) {
                    ASSERT(frm_ques_[i]->enqueue_bulk(rux_frms[i], rux_frms_count[i]));
                    rux_frms_count[i] = 0;
                }
            }
        } // while(sockfd_ != INVALID_SOCKET;

        ASSERT(sockfd_ == INVALID_SOCKET);

        for (i = 0; i < nprocess; i++) {
            delete[] rux_frms[i];
        }
        delete[] rux_frms;
        delete[] rux_frms_count;

        for (i = 0; i < RCVMMSG_SIZE; i++) {
            delete frms[i];
        }

        snd_thread_.join();
        upd_thread_.join();

        while(rux_thread_pool_.size() > 0) {
            auto itr = rux_thread_pool_.begin();
            itr->join();
            rux_thread_pool_.erase(itr);
        }

        while(frm_ques_.size() > 0) {
            auto itr = frm_ques_.begin();
            delete itr->second;
            frm_ques_.erase(itr);
        }
    }


    void _snd_thread() {
        constexpr int SNDMMSG_SIZE = 128;
        mmsghdr msgs[SNDMMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[SNDMMSG_SIZE];
        msghdr *hdr;
        PRUX_FRM frm, frms[SNDMMSG_SIZE];

        int err, i, n;

        while(sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, SNDMMSG_SIZE, 200 * 1000); // wait 200 milliseconds
            if (n > 0) {
                for (i = 0; i < n; i++) {
                    frm = frms[i];
                    char buf[100] = {0};
                    addr2str(&frm->name, buf, 100);
                    DLOG("SNDMSG TO %s\n", buf);

                    hdr                 = &msgs[i].msg_hdr;
                    hdr->msg_name       = &frm->name;
                    hdr->msg_namelen    = frm->namelen;
                    hdr->msg_iov        = &iovecs[i];
                    hdr->msg_iovlen     = 1;
                    iovecs[i].iov_base  = frm->raw;
                    iovecs[i].iov_len   = frm->len;
                }

                err = ::sendmmsg(sockfd_, msgs, n, 0);
                for (i = 0; i < n; i++) {
                    delete frms[i];
                }

                if (err < 0) {
                    ev_.on_error(ErrType::IO_SND, (void*)((int64_t)err));
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
        constexpr int QUE_TIMEOUT = 200 * 1000; // 200 milliseconds

        PRUX_FRM frms[FRMS_MAX], frm;
        int nfrms, i, nmsg;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];
        Rux* rux;

        while (sockfd_ != INVALID_SOCKET) {
            nfrms = que->wait_dequeue_bulk_timed(frms, FRMS_MAX, QUE_TIMEOUT);
            for (i = 0; i < nfrms; i++) {
                frm = frms[i];
                rux = (Rux*)frm->rux;
                if (rux->input(frm)) {
                    ev_.on_error(ErrType::RUX_INPUT, frm);
                    delete frm;
                    continue;
                }

                while (nmsg = rux->recv(msg), nmsg > 0) {
                    ev_.on_message(rux, msg, nmsg);
                }

                delete frm;
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
        constexpr int QUE_TIMEOUT = 200 * 1000;
        constexpr int RUX_MAX = 128;
        Rux::ptr ruxs[RUX_MAX];
        int nruxs, i;
        uint32_t rid;
        uint64_t now_us;
        std::unordered_set<uint32_t> sessions;

        while (sockfd_ != INVALID_SOCKET) {
            nruxs = update_que_.wait_dequeue_bulk_timed(ruxs, RUX_MAX, QUE_TIMEOUT);

            if (nruxs == 0) {
                continue;
            }

            now_us = sys_clock();

            for (i = 0; i < nruxs; i++) {
                rid = ruxs[i]->rid();
                if (sessions.count(rid) == 0) {
                    sessions.insert(rid);
                }
            }

            for (uint32_t rid : sessions) {
                if (sessions_[rid - 1]->output(now_us) < 0) {
                    // TODO: error
                }
            }

            sessions.clear();
        } // while (sockfd_ != INVALID_SOCKET);
    }


    SOCKET                                                                  sockfd_;            // 服务端 UDP 套接字
    uint64_t                                                                start_us_;

    std::thread                                                             rcv_thread_;        // input 线程
    std::thread                                                             snd_thread_;        // output 线程
    std::thread                                                             upd_thread_;        // rux update 线程
    std::list<std::thread>                                                  rux_thread_pool_;   // rux 线程

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>                           output_que_;        // output 队列
    moodycamel::BlockingConcurrentQueue<Rux::ptr>                           update_que_;
    std::unordered_map<int, moodycamel::BlockingConcurrentQueue<PRUX_FRM>*> frm_ques_;          // 帧工作队列;  input 线程为生产者, rux 线程为消费者
    
    std::vector<Rux*>                                                       sessions_;
    TimerScheduler<Rux>                                                     ts_;
    TEvent                                                                  ev_;


    RuxServer(const RuxServer&) = delete;
    RuxServer(const RuxServer&&) = delete;
    RuxServer& operator=(const RuxServer&) = delete;
    RuxServer& operator=(const RuxServer&&) = delete;
}; // class RuxServer;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_RUX_SERVER__
