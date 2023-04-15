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


        static Datagram* get(UdpSession* sess = nullptr, const sockaddr* name = nullptr, socklen_t namelen = sizeof(sockaddr), const uint8_t* data = nullptr, int datalen = 0) {
            return new Datagram(sess, name, namelen, data, datalen);
        }


        static void put(Datagram* dg) {
            delete dg;
        }


        void set_name(const std::string& remote) {
            ASSERT(xq::net::str2addr(remote, &name, (socklen_t*)&namelen));
        }


        void set_name(const sockaddr *addr, socklen_t addrlen) {
            ASSERT(addr && addrlen > 0);
            ::memcpy(&name, addr, addrlen);
            namelen = addrlen;
        }


        void set_data(const uint8_t* data, int datalen) {
            ASSERT(data && datalen > 0 && datalen <= xq::net::UDP_DGX_SIZE);
            ::memcpy(this->data, data, datalen);
            this->datalen = datalen;
        }


        std::string to_string() const {
            char buf[xq::net::UDP_DGX_SIZE * 2 + 500] = {0};
            sprintf(buf, "[%s]:[%s]", net::addr2str(&this->name).c_str(), xq::tools::bin2hex(this->data, this->datalen).c_str());
            return buf;
        }


    private:
        /* -------------------------------------- */
        /// @brief 构造函数
        ///
        /// @param sess 所属会话(META字段)
        ///
        /// @param name 
        ///
        /// @param namelen 
        ///
        /// @param data 
        ///
        /// @param datalen
        ///
        explicit Datagram(UdpSession* sess, const sockaddr* name, socklen_t namelen, const uint8_t* data, int datalen)
            : time_ms(0)
            , sess(sess)
            , namelen(namelen)
            , datalen(datalen)
            , name({0,{0}}) {
            if (name) {
                ::memcpy(&this->name, name, namelen);
            }

            if (data && datalen > 0) {
                ASSERT(datalen < xq::net::UDP_DGX_SIZE);
                ::memcpy(this->data, data, datalen);
            }
            else {
                ::memset(this->data, 0, xq::net::UDP_DGX_SIZE);
            }
        }


        Datagram(const Datagram&) = delete;
        Datagram(const Datagram&&) = delete;
        Datagram& operator=(const Datagram&) = delete;
        Datagram& operator=(const Datagram&&) = delete;
    };
    /* ------------------------------------------------------------------- END Datagram ------------------------------------------------------------------- */


    typedef std::shared_ptr<UdpSession> Ptr;
    typedef int (*RcvCallback)(const Datagram*);


    static Ptr create(const std::string& local_addr = "") {
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


    void set_reuse() {
        constexpr int ON = 1;
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, (char *)&ON, sizeof(ON)));
#ifndef WIN32
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &ON, sizeof(ON)));
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(ON)));
#endif // !WIN32
    }


    void close() {
        stop();

        if (sockfd_ != INVALID_SOCKET) {
            xq::net::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    void run(RcvCallback rcv_cb) {
        thread_ = std::thread(std::bind(&UdpSession::start_rcv, this, rcv_cb));
    }


    void stop() {
        constexpr char buf[1] = { 'X' };
        if (wp_ != -1) {
#ifndef WIN32
            ASSERT(::write(wp_, buf, 1) == 1);
#else
            static sockaddr addr = { 0,{0} };
            static socklen_t addrlen = sizeof(addr);
            if (addr.sa_family == 0) {
                ASSERT(xq::net::str2addr("127.0.0.1:34567", &addr, &addrlen));
            }
            ASSERT(::sendto(wp_, buf, 1, 0, &addr, addrlen) == 1);
#endif // !WIN32
        }
    }


    void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }


    void join_multi_addr(const std::string &multi_ip, const std::string &local_ip) {
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
        ASSERT(!xq::net::make_nonblocking(sockfd_));

        wp_ = xq::net::udp_bind("127.0.0.1", "34567");
        fd_set fds, rfds;
        FD_ZERO(&fds);
        FD_SET(sockfd_, &fds);
        FD_SET(wp_, &fds);

        int n;

        while (1) {
            rfds = fds;
            n = ::select(FD_SETSIZE, &rfds, nullptr, nullptr, nullptr);
            if (n <= 0) {
                continue;
            }

            if (FD_ISSET(wp_, &rfds)) {
                char buf[1];
                if (::recvfrom(wp_, buf, 1, 0, nullptr, nullptr) == 1 && buf[0] == 'X') {
                    xq::net::close(wp_);
                    wp_ = -1;
                    break;
                }
            }

            do {
                Datagram* dg = Datagram::get(this);
                int n = ::recvfrom(sockfd_, (char*)dg->data, UDP_DGX_SIZE, 0, &dg->name, &dg->namelen);
                if (n < 0) {
                    int err = xq::net::error();
                    if (err == WSAEWOULDBLOCK) {
                        break;
                    }
                    std::printf("recv failed: %d\n", err);
                }

                dg->datalen = n;
                dg->time_ms = xq::tools::now_ms();
                n = rcv_cb(dg);
                if (n <= 0) {
                    delete dg;
                }
            } while (1);
        }

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            delete* itr;
            snd_buf_.erase(itr);
        }

        close();
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            Datagram* seg = *itr;
            int n = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
            delete seg;
            snd_buf_.erase(itr++);
            if (n < 0) {
                return -1;
            }
        }

        return 0;
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
        Datagram* segs[IO_RMSG_SIZE] = {nullptr};
        Datagram* seg;
        msghdr* hdr;

        int n = IO_RMSG_SIZE, err;

        fd_set fds, rfds;
        FD_ZERO(&fds);
        FD_SET(sockfd_, &fds);
        FD_SET(rpfd, &fds);

        for (int i = 0; i < n; i++) {
            if (!segs[i]) {
                segs[i] = Datagram::get(this);
            }
            seg = segs[i];

            hdr = &msgs[i].msg_hdr;
            hdr->msg_name = &seg->name;
            hdr->msg_namelen = seg->namelen;
            hdr->msg_iov = &iovecs[i];
            hdr->msg_iovlen = 1;
            iovecs[i].iov_base = seg->data;
            iovecs[i].iov_len = sizeof(seg->data);
        }

        while(1) {
            rfds = fds;
            n = ::select(FD_SETSIZE, &rfds, nullptr, nullptr, nullptr);
            if (n <= 0) {
                continue;
            }

            if (FD_ISSET(rpfd, &rfds)) {
                char buf[1];
                if (::read(rpfd, buf, 1) == 1 && buf[0] == 'X') {
                    ::close(wp_);
                    wp_ = -1;
                    break;
                }
            }

            do {
                n = ::recvmmsg(sockfd_, msgs, IO_RMSG_SIZE, MSG_WAITFORONE, nullptr);
                if (n < 0) {
                    err = error();
                    if (err != EAGAIN && err != EWOULDBLOCK && err != EINTR) {
                        // TODO: error
                        std::printf("recvmmsg failed: %d\n", xq::net::error());
                    }
                    break;
                }
                else if (n == 0) {
                    break;
                }

                int64_t now_ms = xq::tools::now_ms();

                for (int i = 0; i < n; i++) {
                    seg->datalen = msgs[i].msg_len;
                    if (seg->datalen > UDP_DGX_SIZE) {
                        continue;
                    }

                    seg = segs[i];
                    seg->sess = this;
                    seg->datalen = msgs[i].msg_len;
                    seg->time_ms = now_ms;

                    int res = rcv_cb(seg);
                    segs[i] = nullptr;
                    if (res <= 0) {
                        delete seg;
                    }

                    seg = segs[i] = Datagram::get(this);
                    hdr = &msgs[i].msg_hdr;
                    hdr->msg_name = &seg->name;
                    hdr->msg_namelen = seg->namelen;
                    hdr->msg_iov = &iovecs[i];
                    hdr->msg_iovlen = 1;
                    iovecs[i].iov_base = seg->data;
                    iovecs[i].iov_len = UDP_DGX_SIZE;
                }
            } while(1);
        }

        ::close(rpfd);

        for (int i = 0; i < IO_RMSG_SIZE; i++) {
            if (segs[i]) delete segs[i];
        }

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            delete* itr;
            snd_buf_.erase(itr);
        }
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        mmsghdr msgs[IO_SMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_SMSG_SIZE];
        msghdr *hdr;
        Datagram* seg;

        size_t n = 0;
        int res = 0;

        for (auto itr = snd_buf_.begin(); itr != snd_buf_.end(); ++n, ++itr) {
            seg = *itr;
            hdr = &msgs[n].msg_hdr;
            hdr->msg_name = &seg->name;
            hdr->msg_namelen = seg->namelen;
            hdr->msg_iov = &iovecs[n];
            hdr->msg_iovlen = 1;
            iovecs[n].iov_base = seg->data;
            iovecs[n].iov_len = seg->datalen;

            if (n == IO_SMSG_SIZE) {
                res = ::sendmmsg(sockfd_, msgs, n, 0);
                if (res < 0) {
                    // TODO: ...
                }
                n = 0;
            }
        }

        res = ::sendmmsg(sockfd_, msgs, n, 0);
        if (res < 0) {
            // TODO: ...
        }

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            delete *itr;
            snd_buf_.erase(itr);
        }

        return res;
    }
#endif // WIN32


    /* --------------------------------------------------------------------- */
    /// @brief 发送数据, 
    ///        参数seg 在主调函数中必需是 new 运算符创建. 
    ///        主调函数无需调用 delete 删除该对象, 该对象将由 该方法接管.
    /// 
    /// @param seg   需要发送的UdpSession::Datagram
    /// @param force 立即发送数据
    /// 
    /// @return 成功返回 0, 否则返回 -1
    ///
    int send(const Datagram* dg, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        if (dg->datalen > 0) {   
            if (force) {
                int ret = ::sendto(sockfd_, (char*)dg->data, dg->datalen, 0, &dg->name, dg->namelen);
                delete dg;
                return ret;
            }
            snd_buf_.emplace_back(*(Datagram**)&dg);
        }

        return 0;
    }


    int send(const uint8_t* data, size_t datalen, const sockaddr* remote, socklen_t remotelen, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
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
        , addr_({ 0, {0} })
        , addrlen_(sizeof(addr_)) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    SOCKET sockfd_;
    int wp_;
    sockaddr addr_;
    socklen_t addrlen_;
    std::thread thread_;
    std::list<Datagram*> snd_buf_;


    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_UDP_SESSION__
