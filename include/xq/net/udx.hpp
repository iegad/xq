#ifndef __XQ_NET_UDX__
#define __XQ_NET_UDX__


#include "xq/net/basic.hpp"
#include <functional>
#include <xq/third/blockingconcurrentqueue.h>


namespace xq {
namespace net {


/// @brief UDP transfer frame
struct Frame {
    typedef Frame* ptr;

    uint16_t             len;                // raw data's length
    socklen_t            namelen;            // remote sockaddr's length
    sockaddr_storage     name;               // remote sockaddr
    uint8_t              raw[UDP_MTU + 1];   // raw data

    /* META */
    int64_t              time_us;            // receive timestamp(us)


    inline Frame* clone() {
        Frame::ptr pfm = new Frame;
        ::memcpy(pfm, this, sizeof(Frame));
        return pfm;
    }


    static ptr get() {
        return new Frame;
    }


    static void put(ptr f) {
        delete f;
    }


protected:
    Frame()
        : len(0)
        , namelen(sizeof(sockaddr_storage))
        , time_us(0) {
        ::memset(&name, 0, sizeof(name));
        ::memset(raw, 0, sizeof(raw));
    }


    ~Frame() {}


private:
    Frame(const Frame&) = delete;
    Frame(const Frame&&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame& operator=(const Frame&&) = delete;
}; // struct Frame


class Udx;
struct UdxOption {
    typedef void (*PrevRunEvent)(Udx*);
    typedef void (*PostRunEvent)(Udx*);
    typedef void (*PrevStopEvent)(Udx*);
    typedef void (*PostStopEvent)(Udx*);


#ifdef _WIN32
    typedef void (*RecvFrameEvent)(Udx*, int, Frame::ptr);
    typedef void (*PrevSendFrameEvent)(Udx*, Frame::ptr);
    typedef void (*PostSendFrameEvent)(Udx*, int, Frame::ptr);
#else
    typedef void (*RecvFrameEvent)(Udx*, int, Frame::ptr*, int);
    typedef void (*PrevSendFrameEvent)(Udx*, Frame::ptr*, int);
    typedef void (*PostSendFrameEvent)(Udx*, int, Frame::ptr*, int);
#endif // _WIN32


    std::string         endpoint;                   // Endpoint: "0.0.0.0:6688", ":6688"
    PrevRunEvent        prev_run_handler;           // before udx run
    PostRunEvent        post_run_handler;           // after udx run
    PrevStopEvent       prev_stop_handler;          // before udx stop
    PostStopEvent       post_stop_handler;          // after udx stop
    RecvFrameEvent      recv_frame_handler;         // received udx frame
    PrevSendFrameEvent  prev_send_frame_handler;    // before send udx frame
    PostSendFrameEvent  post_send_frame_handler;    // after send udx frame


    UdxOption()
        : endpoint("")
        , prev_run_handler(nullptr)
        , post_run_handler(nullptr)
        , prev_stop_handler(nullptr)
        , post_stop_handler(nullptr)
        , recv_frame_handler(nullptr)
        , prev_send_frame_handler(nullptr)
        , post_send_frame_handler(nullptr)
    {}
}; // struct UdxOption;


class Udx {
public:
    typedef moodycamel::BlockingConcurrentQueue<Frame::ptr> FrameQueue;


    static constexpr int SND_QUE_SIZE = 4 * 1024;


    explicit Udx(const UdxOption* option)
        : sockfd_(INVALID_SOCKET)
        , on_prev_run_(option->prev_run_handler)
        , on_post_run_(option->post_run_handler)
        , on_prev_stop_(option->prev_stop_handler)
        , on_post_stop_(option->post_stop_handler)
        , on_recv_frame_(option->recv_frame_handler)
        , on_prev_send_frame_(option->prev_send_frame_handler)
        , on_post_send_frame_(option->post_send_frame_handler)
        , snd_que_(SND_QUE_SIZE) {
        ASSERT(option->endpoint.size() > 0);

        size_t pos = option->endpoint.rfind(':');
        ASSERT(pos != std::string::npos);
        
        host_ = option->endpoint.substr(0, pos);
        svc_ = option->endpoint.substr(pos + 1);
        if (host_.empty()) {
            host_ = "0.0.0.0";
        }

        ASSERT(host_.size() > 0 && svc_.size() > 0);
    }


    ~Udx() {
        this->shutdown();
    }


    inline SOCKET sockfd() const {
        return sockfd_;
    }


    inline void shutdown() {
        stop();
        wait();
    }


    inline void run() {
        if (sockfd_ != INVALID_SOCKET) {
            return;
        }

        sockfd_ = ::udp_bind(host_.c_str(), svc_.c_str());
        ASSERT(sockfd_ != INVALID_SOCKET);

        if (on_prev_run_) {
            on_prev_run_(this);
        }

        snd_thread_ = std::thread(std::bind(&Udx::_snd_thread, this));
        rcv_thread_ = std::thread(std::bind(&Udx::_rcv_thread, this));

        if (on_post_run_) {
            on_post_run_(this);
        }
    }


    inline void stop() {
        if (on_prev_stop_) {
            on_prev_stop_(this);
        }

        if (sockfd_ != INVALID_SOCKET) {
            ::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    inline void wait() {
        if (rcv_thread_.joinable()) {
            rcv_thread_.join();
        }

        if (snd_thread_.joinable()) {
            snd_thread_.join();
        }

        if (on_post_stop_) {
            on_post_stop_(this);
        }
    }


    inline int join_multicast(const std::string& multi_local_ip, const std::string& multi_route_ip) {
        ASSERT(multi_local_ip.size() > 0 && multi_route_ip.size() > 0);

        int af = xq::net::check_ip_type(multi_route_ip);
        ASSERT(af == AF_INET/* || af == AF_INET6*/);

        ip_mreq mreq;
        ::memset(&mreq, 0, sizeof(mreq));
        if (::inet_pton(af, multi_route_ip.c_str(), &mreq.imr_multiaddr) != 1) {
            return -1;
        }

        if (::inet_pton(af, multi_local_ip.c_str(), &mreq.imr_interface) != 1) {
            return -1;
        }

        if (::setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq))) {
            return -1;
        }

        return 0;
    }


    inline int send(Frame::ptr pfm) {
        return snd_que_.try_enqueue(pfm) ? 0 : -1;
    }


    inline int send(Frame::ptr* pfms, size_t n) {
        return snd_que_.try_enqueue_bulk(pfms, n) ? 0 : -1;
    }


    inline std::string host() const {
        return host_;
    }


    inline std::string svc() const {
        return svc_;
    }


    inline int local_addr(sockaddr* addr, socklen_t* addrlen) const {
        if (sockfd_ == INVALID_SOCKET) {
            return -1;
        }

        return ::getsockname(sockfd_, addr, addrlen);
    }


    inline std::string local_addr_str() const {
        sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        ::memset(&addr, 0, addrlen);

        if (local_addr((sockaddr*)&addr, &addrlen)) {
            return "";
        }

        char buf[ENDPOINT_STR_LEN] = {0};
        if (addr2str(&addr, buf, ENDPOINT_STR_LEN)) {
            return "";
        }

        return buf;
    }


private:
#ifdef _WIN32


    void _rcv_thread() {
        Frame::ptr pfm = Frame::get();
        int n;

        while (INVALID_SOCKET != sockfd_) {
            n = ::recvfrom(sockfd_, (char*)pfm->raw, sizeof(pfm->raw), 0, (sockaddr *)&pfm->name, &pfm->namelen);
            if (n == 0) {
                continue;
            }
            
            if (on_recv_frame_) {
                if (n > 0) {
                    pfm->len = n;
                }
                on_recv_frame_(this, n < 0 ? errcode : 0, pfm);
                pfm = Frame::get();
            }
        }

        Frame::put(pfm);
    }


    void _snd_thread() {
        constexpr int TIMEOUT   = 200 * 1000; // 200 ms
        constexpr int FRM_SIZE  = 128;

        int         err;
        size_t      n, i;
        Frame::ptr  pfm;
        Frame::ptr  pfms[FRM_SIZE];

        while (sockfd_ != INVALID_SOCKET) {
            n = snd_que_.wait_dequeue_bulk_timed(pfms, FRM_SIZE, TIMEOUT);
            for (i = 0; i < n; i++) {
                pfm = pfms[i];
                if (on_prev_send_frame_) {
                    on_prev_send_frame_(this, pfm);
                }

                err = ::sendto(sockfd_, (char*)pfm->raw, pfm->len, 0, (sockaddr*)&pfm->name, pfm->namelen);
                if (on_post_send_frame_) {
                    on_post_send_frame_(this, err >= 0 ? 0 : errcode, pfm);
                }

                Frame::put(pfm);
            }
        }

        do {
            n = snd_que_.try_dequeue_bulk(pfms, FRM_SIZE);
            for (i = 0; i < n; i++) {
                Frame::put(pfms[i]);
            }
        } while (n > 0);
    }


#else


    void _rcv_thread() {
        constexpr int       RCVMMSG_SIZE    = 128;
        constexpr timeval   TIMEOUT         = { .tv_sec = 0, .tv_usec = 200 * 1000};

        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        mmsghdr msgs[RCVMMSG_SIZE];
        msghdr* hdr;
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[RCVMMSG_SIZE];
        ::memset(iovecs, 0, sizeof(iovecs));

        Frame::ptr pfms[RCVMMSG_SIZE] = {nullptr};
        Frame::ptr pfm;

        int i, n, err;
//        int64_t now_us;

        for (i = 0; i < RCVMMSG_SIZE; i++) {
            pfm = pfms[i] = Frame::get();

            hdr                 = &msgs[i].msg_hdr;
            hdr->msg_name       = &pfm->name;
            hdr->msg_namelen    = pfm->namelen;
            hdr->msg_iov        = &iovecs[i];
            hdr->msg_iovlen     = 1;
            iovecs[i].iov_base  = pfm->raw;
            iovecs[i].iov_len   = sizeof(pfm->raw);
        }

        while(sockfd_ != INVALID_SOCKET) {
            err = 0;
            n = ::recvmmsg(sockfd_, msgs, RCVMMSG_SIZE, MSG_WAITFORONE, nullptr);
            if (n == 0) {
                continue;
            } else if (n < 0) {
                err = errcode;
                if (err == EAGAIN || err == EINTR) {
                    continue;
                }
            }

            for (i = 0; i < n; i++) {
                pfms[i]->len = msgs[i].msg_len;
                // TODO
            }

            if (on_recv_frame_) {
                on_recv_frame_(this, err, pfms, n);
            }

            for (i = 0; i < n; i++) {
                pfm = pfms[i] = Frame::get();

                hdr                 = &msgs[i].msg_hdr;
                hdr->msg_name       = &pfm->name;
                hdr->msg_namelen    = pfm->namelen;
                hdr->msg_iov        = &iovecs[i];
                hdr->msg_iovlen     = 1;
                iovecs[i].iov_base  = pfm->raw;
                iovecs[i].iov_len   = sizeof(pfm->raw);
            }
        }

        for (i = 0; i < RCVMMSG_SIZE; i++) {
            Frame::put(pfms[i]);
        }
    }


    void _snd_thread() {
        constexpr int SNDMMSG_SIZE  = 128;
        constexpr int TIMEOUT       = 200 * 1000;

        mmsghdr msgs[SNDMMSG_SIZE];
        msghdr* hdr;
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[SNDMMSG_SIZE];
        Frame::ptr pfm;
        Frame::ptr pfms[SNDMMSG_SIZE] = {nullptr};

        int err;
        size_t i, n;

        while (sockfd_ != INVALID_SOCKET) {
            n = snd_que_.wait_dequeue_bulk_timed(pfms, SNDMMSG_SIZE, TIMEOUT);
            for (i = 0; i < n; i++) {
                pfm = pfms[i];

                hdr                 = &msgs[i].msg_hdr;
                hdr->msg_name       = &pfm->name;
                hdr->msg_namelen    = pfm->namelen;
                hdr->msg_iov        = &iovecs[i];
                hdr->msg_iovlen     = 1;
                iovecs[i].iov_base  = pfm->raw;
                iovecs[i].iov_len   = pfm->len;
            }

            if (on_prev_send_frame_) {
                on_prev_send_frame_(this, pfms, (int)n);
            }

            err = ::sendmmsg(sockfd_, msgs, n, 0);
            if (on_post_send_frame_) {
                on_post_send_frame_(this, err < 0 ? errcode : 0, pfms, n);
            }

            for (i = 0; i < n; i++) {
                Frame::put(pfms[i]);
            }
        }

        do {
            n = snd_que_.try_dequeue_bulk(pfms, SNDMMSG_SIZE);
            for (i = 0; i < n; i++) {
                Frame::put(pfms[i]);
            }
        } while (n > 0);
    }


#endif // _WIN32


    SOCKET                          sockfd_;

    UdxOption::PrevRunEvent         on_prev_run_;
    UdxOption::PostRunEvent         on_post_run_;
    UdxOption::PrevStopEvent        on_prev_stop_;
    UdxOption::PostStopEvent        on_post_stop_;
    UdxOption::RecvFrameEvent       on_recv_frame_;
    UdxOption::PrevSendFrameEvent   on_prev_send_frame_;
    UdxOption::PostSendFrameEvent   on_post_send_frame_;

    std::thread         rcv_thread_;
    std::thread         snd_thread_;
    std::string         host_;
    std::string         svc_;

    FrameQueue          snd_que_;


    Udx(const Udx&) = delete;
    Udx(const Udx&&) = delete;
    Udx& operator=(const Udx&) = delete;
    Udx& operator=(const Udx&&) = delete;
}; // class __XQ_NET_UDX__;


} // namespace net
} // namespace xq


#endif // !__XQ_NET_UDP_SESSION__
