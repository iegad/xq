#ifndef __XQ_NET_RUX_CLIENT__
#define __XQ_NET_RUX_CLIENT__


#include <string>
#include <unordered_map>
#include <functional>
#include <xq/third/blockingconcurrentqueue.h>
#include "xq/net/rux.hpp"


namespace xq {
namespace net {


// ############################################################################################################
// Rux 客户端
// ############################################################################################################
template<class TEvent>
class RuxClient {
public:


    // ========================================================================================================
    // 构造函数.
    // ========================================================================================================
    explicit RuxClient(uint32_t rid)
        : sockfd_(INVALID_SOCKET)
        , rid_(rid)
        , output_que_() {
        ASSERT(rid > 0 && rid <= RUX_RID_MAX);
    }


    ~RuxClient() {
        this->stop();
        this->wait();

        for (auto& itr : node_map_) {
            delete itr.second;
        }
    }


    // ========================================================================================================
    // 启动服务
    //      该方法分为同步和异步启动.
    //      异步启动需要通过 wait方法等待 run 的结束
    // -------------------------------
    // @async:  是否异步启动
    // ========================================================================================================
    void run(bool async = true) {
        if (async) {
            rcv_thread_ = std::thread(std::bind(&RuxClient::_rcv_thread, this));
            return;
        }
        
        _rcv_thread();
    }


    // ========================================================================================================
    // 停止服务
    // ========================================================================================================
    void stop() {
        if (sockfd_ != INVALID_SOCKET) {
            close(sockfd_);
            sockfd_ = INVALID_SOCKET;
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
    // 连接服务节点
    // -------------------------------
    // @endpoint: 服务节点
    // ========================================================================================================
    void connect_node(const char* endpoint, uint64_t now_us) {
        Rux* rux = new Rux(rid_, now_us, output_que_);
        rux->addr()->ss_family = AF_INET;
        ASSERT(!str2addr(endpoint, rux->addr(), rux->addrlen()));
        rux->reset(now_us);
        rux->set_state(0);
        node_map_.insert(std::make_pair(endpoint, rux));
    }


    // ========================================================================================================
    // 向指定结点发送消息
    // -------------------------------
    // @endpoint: 服务节点
    // @msg:      消息体
    // @msglen:   消息长度
    // ========================================================================================================
    int send(const char* endpoint, const uint8_t* msg, uint16_t msglen) {
        auto itr = node_map_.find(endpoint);
        if (itr == node_map_.end()) {
            return -1;
        }

        return itr->second->send(msg, msglen);
    }


private:
#ifdef WIN32


    // ========================================================================================================
    // IO input 线程
    // ========================================================================================================
    void _rcv_thread() {
        constexpr int ENDPOINT_SIZE = INET6_ADDRSTRLEN + 7;

        // Step 1, init udp socket
        sockfd_ = udp_bind("0.0.0.0", "0");
        ASSERT(sockfd_ != INVALID_SOCKET);

        // Step 2, start output thread
        snd_thread_ = std::thread(std::bind(&RuxClient::_snd_thread, this));

        // Step 3, start input thread
        upd_thread_ = std::thread(std::bind(&RuxClient::_update_thread, this));

        // Step 4, start loop recfrom
        char endpoint[ENDPOINT_SIZE];
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];
        PRUX_FRM frm = new RUX_FRM;
        int n;
        Rux* rux;

        while (sockfd_ != INVALID_SOCKET) {
            n = recvfrom(sockfd_, (char*)frm->raw, sizeof(frm->raw), 0, (sockaddr*)&frm->name, &frm->namelen);
            if (n < 0) {
                ev_.on_error(ErrType::IO_RCV, (void*)((int64_t)errcode));
                continue;
            }

            frm->len = n;
            if (n > RUX_MTU || n < RUX_FRM_HDR_SIZE || frm->check()) {
                ev_.on_error(ErrType::IO_RCV_FRAME, frm);
                continue;
            }

            frm->time_us = sys_clock();
            ::memset(endpoint, 0, sizeof(endpoint));
            ASSERT(!addr2str(&frm->name, endpoint, ENDPOINT_SIZE));
            auto itr = node_map_.find(endpoint);
            if (itr == node_map_.end()) {
                continue;
            }

            rux = itr->second;
            if (rux->input(frm)) {
                continue;
            }

            while (n = rux->recv(msg), n > 0) {
                ev_.on_message(rux, msg, n);
            }
        }

        upd_thread_.join();
        snd_thread_.join();

        delete frm;
        delete[] msg;
    }


    // ========================================================================================================
    // IO output 线程
    // ========================================================================================================
    void _snd_thread() {
        constexpr int FRMS_MAX = 64;

        PRUX_FRM frms[FRMS_MAX], frm;
        int n, i;

        while (sockfd_ != INVALID_SOCKET) {
            n = output_que_.wait_dequeue_bulk_timed(frms, FRMS_MAX, 50000);
            for (i = 0; i < n; i++) {
                frm = frms[i];
                if (::sendto(sockfd_, (char*)frm->raw, frm->len, 0, (sockaddr*)&frm->name, frm->namelen) < 0) {
                    ev_.on_error(ErrType::IO_SND, (void*)((int64_t)errcode));
                }
                delete frm;
            }
        }

        while (n = output_que_.try_dequeue_bulk(frms, FRMS_MAX), n > 0) {
            for (i = 0; i < n; i++) {
                delete frms[i];
            }
        }
    }
#else
    void _rcv_thread() {
        constexpr int RCVMMSG_SIZE = 128;
        constexpr static timeval TIMEOUT{0, 500000};

        sockfd_ = udp_bind("0.0.0.0", "0");
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        snd_thread_ = std::thread(std::bind(&RuxClient::_snd_thread, this));
        upd_thread_ = std::thread(std::bind(&RuxClient::_update_thread, this));

        int i, n = RCVMMSG_SIZE, err;
        uint64_t now_us;

        mmsghdr msgs[RCVMMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));
        msghdr *hdr;

        iovec iovecs[RCVMMSG_SIZE];
        PRUX_FRM frms[RCVMMSG_SIZE] = {nullptr}, frm;

        char endpoint[INET6_ADDRSTRLEN + 7];
        std::unordered_map<std::string, Rux*>::iterator itr;
        Rux* rux;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];

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
                    ev_.on_error(ErrType::IO_RCV, (void*)((int64_t)errcode));
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
                ::memset(endpoint, 0, sizeof(endpoint));
                ASSERT(!addr2str(&frm->name, endpoint, sizeof(endpoint)));
                itr = node_map_.find(endpoint);
                if (itr == node_map_.end()) {
                    continue;
                }

                rux = itr->second;
                if (rux->input(frm)) {
                    continue;
                }

                while(n = rux->recv(msg), n > 0) {
                    ev_.on_message(rux, msg, n);
                }
            }
        } // while(sockfd_ != INVALID_SOCKET;

        for (i = 0; i < RCVMMSG_SIZE; i++) {
            delete frms[i];
        }

        delete[] msg;

        upd_thread_.join();
        snd_thread_.join();
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
                    ev_.on_error(ErrType::IO_SND, (void*)((int64_t)errcode));
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


    void _update_thread() {
        std::unordered_map<std::string, Rux*>::iterator itr;
        Rux* rux;
        uint64_t now_us;
#ifndef WIN32
        timeval timeout = {0, 0};
#endif

        while (sockfd_ != INVALID_SOCKET) {
            now_us = sys_clock();

            itr = node_map_.begin();
            while (itr != node_map_.end()) {
                rux = itr->second;
                if (rux->output(now_us) < 0) {
                    ev_.on_error(ErrType::RUX_OUTPUT, rux);
                }
                itr++;
            }
#ifdef WIN32
            std::this_thread::sleep_for(std::chrono::microseconds(500));
#else
            timeout.tv_usec = 500;
            ::select(0, nullptr, nullptr, nullptr, &timeout);
#endif
        }
    }


    SOCKET                                          sockfd_;
    uint32_t                                        rid_;           // rux id

    std::thread                                     rcv_thread_;    // io input 线程
    std::thread                                     snd_thread_;    // io output 线程
    std::thread                                     upd_thread_;    // update 线程

    std::unordered_map<std::string, Rux*>           node_map_;      // service node's map
    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   output_que_;

    TEvent                                          ev_;


    RuxClient(const RuxClient&) = delete;
    RuxClient(const RuxClient&&) = delete;
    RuxClient& operator=(const RuxClient&) = delete;
    RuxClient& operator=(const RuxClient&&) = delete;
}; // class RuxClient;


} // namespace net
} // namespace xq


#endif // __XQ_NET_RUX_CLIENT__
