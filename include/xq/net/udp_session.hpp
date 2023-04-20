#ifndef __XQ_NET_UDP_SESSION__
#define __XQ_NET_UDP_SESSION__


#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"
#include <list>


namespace xq {
namespace net {


/* -------------------------------------- */
/// @brief UDP会话 该UDP会话为Udp协议的扩展IO
///
/// @note  1, UdpSession 只允许接收 UDP_DGX_SIZE 大小的数据;
///        2, 支持IPV4组播;
///
class UdpSession {
public:
    /* ------------------------------------------------------------------- BEG Datagram ------------------------------------------------------------------- */
    /// @brief UdpSession 数据报
    ///
    struct Datagram {
        /* ------------------- META 字段 ------------------- */
        // 数据接收时间
        int64_t time_ms;
        // 数据所属会话
        UdpSession* sess;
        
        /* ------------------- 数据相关 字段 ------------------- */
        // 地址长度
        int namelen;
        // 数据长度
        int datalen;
        // 数据来源地址
        sockaddr name;
        // 数据
        uint8_t data[xq::net::UDP_DGX_SIZE + 1];


        static __inline__ Datagram* get(UdpSession* sess = nullptr, const sockaddr* name = nullptr, socklen_t namelen = sizeof(sockaddr), const uint8_t* data = nullptr, int datalen = 0) {
            static int count = 0;

            std::printf(" ++++++++++ get: %d ++++++++++\n", ++count);
            Datagram* dg = (Datagram*)::malloc(sizeof(Datagram));
            ASSERT(dg);

            if (sess) {
                dg->sess = sess;
            }
            else {
                dg->sess = nullptr;
            }

            if (name) {
                ::memcpy(&dg->name, name, namelen);
            }
            else {
                ::memset(&dg->name, 0, sizeof(namelen));
            }
            dg->namelen = namelen;

            dg->datalen = datalen;
            if (data) {
                ASSERT(datalen > 0);
                ::memcpy(dg->data, data, datalen);
            }

            return dg;
        }


        static __inline__  void put(Datagram* dg) {
            static int count = 0;

            std::printf(" ---------- put: %d ----------\n", ++count);
            ::free(dg);
        }
    };
    /* ------------------------------------------------------------------- END Datagram ------------------------------------------------------------------- */


    typedef std::shared_ptr<UdpSession> Ptr;
    typedef int (*RcvCallback)(const Datagram*);


    static __inline__ Ptr create(const std::string& local_addr = "") {
        std::string ip = "0.0.0.0", port = "0";

        if (!local_addr.empty()) {
            size_t pos = local_addr.rfind(':');
            if (pos == std::string::npos) {
                return nullptr;
            }

            if (pos == 0) {
                ip = "0.0.0.0";
            }

            port = local_addr.substr(pos + 1);
        }

        sockaddr addr{ 0, {0} };
        socklen_t addrlen = sizeof(addr);
        
        SOCKET sockfd = udp_bind(ip.c_str(), port.c_str(), &addr, &addrlen);
        if (sockfd == INVALID_SOCKET) {
            return nullptr;
        }

        return Ptr(new UdpSession(sockfd, &addr, addrlen));
    }


    ~UdpSession() {
        close();
    }


    void __inline__ set_reuse() {
        constexpr int ON = 1;
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, (char *)&ON, sizeof(ON)));
#ifndef WIN32
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &ON, sizeof(ON)));
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(ON)));
#endif // !WIN32
    }


    void __inline__ close() {
        stop();

        for (int i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }
        nsnd_buf_ = 0;

        if (sockfd_ != INVALID_SOCKET) {
            xq::net::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    void __inline__ run(RcvCallback rcv_cb) {
        thread_ = std::thread(std::bind(&UdpSession::start_rcv, this, rcv_cb));
    }


    void __inline__ stop() {
        if (wp_ != -1) {
#ifndef WIN32
            static constexpr char buf[1] = { 'X' };
            ASSERT(::write(wp_, buf, 1) == 1);
#else
            xq::net::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
            wp_ = -1;
#endif // !WIN32
        }
    }


    void __inline__ wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }


    void __inline__ join_multi_addr(const std::string &multi_ip, const std::string &local_ip) {
        int af = xq::net::check_ip_type(multi_ip);
        ASSERT(af == AF_INET/* || af == AF_INET6*/);

        ip_mreq mreq;
        ::memset(&mreq, 0, sizeof(mreq));
        ASSERT(::inet_pton(af, multi_ip.c_str(), &mreq.imr_multiaddr) == 1);
        ASSERT(::inet_pton(af, local_ip.c_str(), &mreq.imr_interface) == 1);
        ASSERT(!::setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)));
    }


#ifdef WIN32
    void start_rcv(RcvCallback rcv_cb) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(rcv_cb && "rcv_cb cannot be null");

        wp_ = 0;
        int i, n, err;
        Datagram* dg = Datagram::get(this);

        while (wp_ != -1) {
            n = ::recvfrom(sockfd_, (char*)dg->data, UDP_DGX_SIZE, 0, &dg->name, &dg->namelen);
            if (n > 0) {
                dg->datalen = n;
                dg->time_ms = xq::tools::now_ms();
                n = rcv_cb(dg);
                if (n < 0) {
                    break;
                }
                else if (n > 0) {
                    dg = Datagram::get(this);
                }
            }
            else if (n < 0) {
                err = xq::net::error();
                std::printf("recv failed: %d\n", err);
            }
        }

        Datagram::put(dg);
        for (i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }
        nsnd_buf_ = 0;

        close();
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        int n = 0, i;
        for (i = 0; i < nsnd_buf_; i++) {
            Datagram* dg = snd_buf_[i];
            if (n >= 0) {
                n = ::sendto(sockfd_, (char*)dg->data, dg->datalen, 0, &dg->name, dg->namelen);
            }
            Datagram::put(dg);
        }

        nsnd_buf_ = 0;
        return n >= 0 ? i : n;
    }
#else

    void start_rcv(RcvCallback rcv_cb) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(rcv_cb && "rcv_cb cannot be null");
        ASSERT(!xq::net::make_nonblocking(sockfd_));

        int p[2];
        ASSERT(!pipe(p));
        int rpfd = p[0];
        wp_ = p[1];

        mmsghdr msgs[IO_RMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_RMSG_SIZE];
        Datagram* dgs[IO_RMSG_SIZE] = {nullptr};
        Datagram* dg;
        msghdr* hdr;

        int i, n = IO_RMSG_SIZE, nready, err, res;

        fd_set fds, rfds;
        FD_ZERO(&fds);
        FD_SET(sockfd_, &fds);
        FD_SET(rpfd, &fds);

        for (i = 0; i < n; i++) {
            if (!dgs[i]) {
                dgs[i] = Datagram::get(this);
            }
            dg = dgs[i];

            hdr = &msgs[i].msg_hdr;
            hdr->msg_name = &dg->name;
            hdr->msg_namelen = dg->namelen;
            hdr->msg_iov = &iovecs[i];
            hdr->msg_iovlen = 1;
            iovecs[i].iov_base = dg->data;
            iovecs[i].iov_len = sizeof(dg->data);
        }

        while(wp_ != -1) {
            rfds = fds;
            nready = ::select(FD_SETSIZE, &rfds, nullptr, nullptr, nullptr);
            if (nready <= 0) {
                continue;
            }

            for (i = 0; i < nready; i++) {
                if (FD_ISSET(rpfd, &rfds)) {
                    char buf[1];
                    if (::read(rpfd, buf, 1) == 1 && buf[0] == 'X') {
                        ::close(wp_);
                        wp_ = -1;
                        break;
                    }
                    continue;
                }

                do {
                    n = ::recvmmsg(sockfd_, msgs, IO_RMSG_SIZE, MSG_WAITFORONE, nullptr);
                    if (n < 0) {
                        err = error();
                        if (err != EAGAIN && err != EINTR) {
                            // TODO: error
                            std::printf("recvmmsg failed: %d\n", xq::net::error());
                        }
                        break;
                    }
                    else if (n == 0) {
                        break;
                    }

                    int64_t now_ms = xq::tools::now_ms();

                    for (i = 0; i < n; i++) {
                        dg->datalen = msgs[i].msg_len;
                        if (dg->datalen > UDP_DGX_SIZE) {
                            continue;
                        }

                        dg = dgs[i];
                        dg->sess = this;
                        dg->datalen = msgs[i].msg_len;
                        dg->time_ms = now_ms;

                        res = rcv_cb(dg);
                        if (res < 0) {
                            break;
                        }
                        else if (res > 0) {
                            dg = dgs[i] = Datagram::get(this);
                            hdr = &msgs[i].msg_hdr;
                            hdr->msg_name = &dg->name;
                            hdr->msg_namelen = dg->namelen;
                            hdr->msg_iov = &iovecs[i];
                            hdr->msg_iovlen = 1;
                            iovecs[i].iov_base = dg->data;
                            iovecs[i].iov_len = UDP_DGX_SIZE;
                        }
                    }
                } while (1);
            } // for
        }

        ::close(rpfd);

        for (i = 0; i < IO_RMSG_SIZE; i++) {
            if (dgs[i]) Datagram::put(dgs[i]);
        }

        for (i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }
        nsnd_buf_ = 0;
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        mmsghdr msgs[IO_SMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_SMSG_SIZE];
        msghdr *hdr;
        Datagram* dg;

        size_t n = 0;
        int res = 0, i;

        for (i = 0; i < nsnd_buf_; i++, n++) {
            dg = snd_buf_[i];
            hdr = &msgs[n].msg_hdr;
            hdr->msg_name = &dg->name;
            hdr->msg_namelen = dg->namelen;
            hdr->msg_iov = &iovecs[n];
            hdr->msg_iovlen = 1;
            iovecs[n].iov_base = dg->data;
            iovecs[n].iov_len = dg->datalen;
        }

        if (res >= 0) {
            res = ::sendmmsg(sockfd_, msgs, n, 0);
        }

        for (i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }

        nsnd_buf_ = 0;
        return res;
    }
#endif // WIN32


    /* --------------------------------------------------------------------- */
    /// @brief 发送数据, 
    ///        参数dg 在主调函数中必需是 new 运算符创建. 
    ///        主调函数无需调用 delete 删除该对象, 该对象将由 该方法接管.
    /// 
    /// @param dg   需要发送的UdpSession::Datagram
    /// @param force 立即发送数据
    /// 
    /// @return 成功返回 0, 否则返回 -1
    ///
    int send(const Datagram* dg, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(dg);

        if (dg->datalen > 0) {   
            if (force) {
                int ret = ::sendto(sockfd_, (char*)dg->data, dg->datalen, 0, &dg->name, dg->namelen);
                Datagram::put(*(Datagram**)&dg);
                return ret;
            }

            if (nsnd_buf_ < IO_SMSG_SIZE) {
                snd_buf_[nsnd_buf_++] = *(Datagram**)&dg;
            }
            else if (flush() < 0) {
                return -1;
            }
        }

        return 0;
    }


    int send(const uint8_t* data, size_t datalen, const sockaddr* remote, socklen_t remotelen, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(data && datalen > 0);

        if (force) {
#ifndef WIN32
            return ::sendto(sockfd_, data, datalen, 0, remote, remotelen);
#else
            return ::sendto(sockfd_, (const char*)data, datalen, 0, remote, remotelen);
#endif // !WIN32
        }

        Datagram* dg = Datagram::get(this, remote, remotelen, data, datalen);
        return this->send(dg);
    }


private:
    UdpSession(SOCKET sockfd, const sockaddr *addr, socklen_t addrlen)
        : sockfd_(sockfd)
        , wp_(-1)
        , nsnd_buf_(0)
        , addrlen_(addrlen) {
        ::memcpy(&addr_, addr, addrlen);
        ::memset(snd_buf_, 0, sizeof(snd_buf_));
    }


    SOCKET sockfd_;
    int wp_;
    int nsnd_buf_;
    sockaddr addr_;
    socklen_t addrlen_;
    std::thread thread_;
    Datagram* snd_buf_[IO_SMSG_SIZE];


    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_UDP_SESSION__
