#ifndef __XQ_NET_UDX__
#define __XQ_NET_UDX__


#include <functional>
#include <xq/third/blockingconcurrentqueue.h>
#include "xq/net/net.hpp"


namespace xq {
namespace net {


/// @brief UDP transfer frame
struct Frame {
    typedef Frame* ptr;

    uint16_t         len              = 0;                          // raw data's length
    socklen_t        namelen          = sizeof(sockaddr_storage);   // remote sockaddr's length
    sockaddr_storage name             = {};                         // remote sockaddr
    uint8_t          raw[UDP_MTU + 1] = {};                         // raw data

    /* META */
    int64_t          time_us = 0;       // receive timestamp(us)
    void*            ex      = nullptr; // extension


    /// @brief Default constructor.
    explicit Frame() 
    {}


    /// @brief Constructor
    /// @param addr     remote address
    /// @param addrlen  remote address' length
    explicit Frame(const sockaddr_storage* addr, socklen_t addrlen)
        : namelen(addrlen) {
        ASSERT(addr && addrlen >= sizeof(sockaddr))
        ::memcpy(&name, addr, addrlen);
    }


    /// @brief Copy constructor
    explicit Frame(const Frame& f)
        : len(f.len)
        , namelen(f.namelen)
        , time_us(f.time_us)
        , ex(f.ex) {
        ::memcpy(&name, &f.name, namelen);
        ::memcpy(raw, f.raw, len);
    }


private:
    Frame(const Frame&&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame& operator=(const Frame&&) = delete;
}; // struct Frame


/// @brief  UDP extension
/// @tparam TEvent event class
/// @note   TEvent must implement: 
///          1, void on_run(Udx<T>* provider); 
///          2, void on_stop(Udx<T>* provider);
///          3, WIN: void on_recv(Udx<T>* provider, int err, Frame::ptr pFrame); / !WIN: void on_recv(Udx<T>* provider, Frame::ptr pFrameArray, int nArray);
///          4, void on_send(Udx<T>* provider, int err, Frame::ptr pFrame);
template<class TEvent>
class Udx {
public:
    typedef Udx<TEvent>* ptr;


    /// @brief Constructor
    /// @param endpoint Local endpoint
    /// @param ev       Event object's pointer
    explicit Udx(const std::string& endpoint, TEvent* ev)
        : sockfd_(INVALID_SOCKET)
        , ev_(ev)
        , snd_que_(FRAME_QUE_SIZE) {
        ASSERT(endpoint.size() && ev);

        size_t pos = endpoint.rfind(':');
        ASSERT(pos != std::string::npos);

        host_ = endpoint.substr(0, pos);
        svc_ = endpoint.substr(pos + 1);
        if (host_.empty()) {
            host_ = "0.0.0.0";
        }

        ASSERT(host_.size() > 0 && svc_.size() > 0);
    }


    /// @brief Destructor
    ~Udx() {
        this->shutdown();
    }


    /// @brief  Udx's send queue's reference
    FrameQueue& snd_que() {
        return snd_que_;
    }


    /// @brief raw sockfd
    SOCKET sockfd() const {
        return sockfd_;
    }


    /// @brief Event's reference
    TEvent& ev() {
        return svc_;
    }


    /// @brief Shutdown udx
    void shutdown() {
        stop();
        wait();
    }


    /// @brief check udx is running
    bool running() const {
        return sockfd_ != INVALID_SOCKET;
    }


    /// @brief Run udx
    /// @param async Run asynchronously default true
    void run(bool async = true) {
        if (sockfd_ != INVALID_SOCKET) {
            return;
        }

        sockfd_ = ::udp_bind(host_.c_str(), svc_.c_str());
        ASSERT(sockfd_ != INVALID_SOCKET);

        if (async) {
            rcv_thread_ = std::thread(std::bind(&Udx::_rcv_thread, this));
            return;
        }
        
        _rcv_thread();
    }


    /// @brief Stop udx
    void stop() {
        if (sockfd_ != INVALID_SOCKET) {
            ::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    /// @brief Wait for UDX to stop running
    void wait() {
        if (rcv_thread_.joinable()) {
            rcv_thread_.join();
        }

        this->clear_snd_que();
    }


    /// @brief Udx join the multi cast address
    /// @param multi_local_ip local ip
    /// @param multi_cast_ip multi cast ip
    /// @return 0 on success or -1 on failure.
    int join_multicast(const std::string& multi_local_ip, const std::string& multi_cast_ip) {
        ASSERT(multi_local_ip.size() > 0 && multi_cast_ip.size() > 0);

        int af = xq::net::check_ip_family(multi_cast_ip);
        ASSERT(af == AF_INET/* || af == AF_INET6*/);

        ip_mreq mreq;
        ::memset(&mreq, 0, sizeof(mreq));
        if (::inet_pton(af, multi_cast_ip.c_str(), &mreq.imr_multiaddr) != 1) {
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


    /// @brief Send frame
    /// @param pfm Frame to be sended
    /// @return 0 on success or -1 on failure.
    int send(Frame::ptr pfm) {
        return snd_que_.try_enqueue(pfm) ? 0 : -1;
    }


    /// @brief Send multible frames.
    /// @param pfms Frames to be sended
    /// @param n    Size of pfms
    /// @return 0 on success or -1 on failure.
    int send(Frame::ptr* pfms, size_t n) {
        return snd_que_.try_enqueue_bulk(pfms, n) ? 0 : -1;
    }


    const char* host() const {
        return host_.c_str();
    }


    const char* svc() const {
        return svc_.c_str();
    }


    int local_addr(sockaddr* addr, socklen_t* addrlen) const {
        if (sockfd_ == INVALID_SOCKET) {
            return -1;
        }

        return ::getsockname(sockfd_, addr, addrlen);
    }


    std::string local_addr_str() const {
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


    void clear_snd_que() {
        int n, i;
        Frame::ptr pfms[128];

        do {
            n = snd_que_.try_dequeue_bulk(pfms, 128);
            for (i = 0; i < n; i++) {
                delete pfms[i];
            }
        } while (n > 0);
    }


private:


#ifdef _WIN32


    void _rcv_thread() {
        Frame::ptr pfm = new Frame;
        int n, err;

        std::thread snd_thread(std::bind(&Udx::_snd_thread, this));
        ev_->on_run(this);

        while (INVALID_SOCKET != sockfd_) {
            n = ::recvfrom(sockfd_, (char*)pfm->raw, sizeof(pfm->raw), 0, (sockaddr *)&pfm->name, &pfm->namelen);
            if (n == 0) {
                continue;
            }
            
            if (n < 0) {
                err = errcode;
            }
            else if (n > UDP_MTU) {
                err = -1;
            }
            else {
                err = 0;
                pfm->len = n;
                pfm->time_us = sys_time();
            }

            ev_->on_recv(this, err, pfm);
            pfm = new Frame;
        }

        delete pfm;
        snd_thread.join();
        ev_->on_stop(this);
    }


    void _snd_thread() {
        constexpr int TIMEOUT  = 200 * 1000; // 200 ms
        constexpr int FRM_SIZE = 128;

        int         err;
        size_t      n, i;
        Frame::ptr  pfm;
        Frame::ptr  pfms[FRM_SIZE];

        while (sockfd_ != INVALID_SOCKET) {
            n = snd_que_.wait_dequeue_bulk_timed(pfms, FRM_SIZE, TIMEOUT);
            for (i = 0; i < n; i++) {
                pfm = pfms[i];
                err = ::sendto(sockfd_, (char*)pfm->raw, pfm->len, 0, (sockaddr*)&pfm->name, pfm->namelen);
                ev_->on_send(this, err >= 0 ? 0 : errcode, pfm);
                delete pfm;
            }
        }
    }


#else


    void _rcv_thread() {
        constexpr int       RCVMMSG_SIZE    = 128;
        constexpr timeval   TIMEOUT         = { .tv_sec = 0, .tv_usec = 200 * 1000};

        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        std::thread snd_thread(std::bind(&Udx::_snd_thread, this));

        ev_->on_run(this);

        mmsghdr msgs[RCVMMSG_SIZE];
        msghdr* hdr;
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[RCVMMSG_SIZE];
        ::memset(iovecs, 0, sizeof(iovecs));

        Frame::ptr pfms[RCVMMSG_SIZE] = {nullptr};
        Frame::ptr pfm;

        int i, n, err;
        int64_t now_us;

        for (i = 0; i < RCVMMSG_SIZE; i++) {
            pfm = pfms[i] = new Frame;

            hdr                 = &msgs[i].msg_hdr;
            hdr->msg_name       = &pfm->name;
            hdr->msg_namelen    = pfm->namelen;
            hdr->msg_iov        = &iovecs[i];
            hdr->msg_iovlen     = 1;
            iovecs[i].iov_base  = pfm->raw;
            iovecs[i].iov_len   = sizeof(pfm->raw);
        }

        while(sockfd_ != INVALID_SOCKET) {
            n = ::recvmmsg(sockfd_, msgs, RCVMMSG_SIZE, MSG_WAITFORONE, nullptr);
            if (n == 0) {
                continue;
            }
            else if (n < 0) {
                err = errcode;
                if (err == EAGAIN || err == EINTR) {
                    continue;
                }
            }
            else {
                err = 0;
            }

            now_us = sys_time();

            for (i = 0; i < n; i++) {
                pfm = pfms[i];
                pfm->time_us = now_us;
                pfm->len = msgs[i].msg_len;
            }

            ev_->on_recv(this, pfms, n);

            for (i = 0; i < n; i++) {
                pfm = pfms[i] = new Frame;

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
            delete pfms[i];
        }

        snd_thread.join();
        ev_->on_stop(this);
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

            err = ::sendmmsg(sockfd_, msgs, n, 0);
            ev_->on_send(this, err < 0 ? errcode : 0, nullptr);

            for (i = 0; i < n; i++) {
                delete pfms[i];
            }
        }
    }


#endif // _WIN32


    SOCKET  sockfd_;
    TEvent* ev_;

    std::thread rcv_thread_;
    std::string host_;
    std::string svc_;

    FrameQueue snd_que_;


    Udx(const Udx&) = delete;
    Udx(const Udx&&) = delete;
    Udx& operator=(const Udx&) = delete;
    Udx& operator=(const Udx&&) = delete;
}; // class __XQ_NET_UDX__;


} // namespace net
} // namespace xq


#endif // !__XQ_NET_UDP_SESSION__
