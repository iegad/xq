#ifndef __XQ_NET_UDP_SESSION__
#define __XQ_NET_UDP_SESSION__


#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"
#include <list>


namespace xq {
namespace net {


class UdpSession {
public:
    // ------------------------------------------------------------------- BEG Segment -------------------------------------------------------------------
    // Udp 传输分组
    struct Segment {
        int namelen;
        int datalen;
        int64_t time_ms;
        UdpSession* sess;
        sockaddr name;
        uint8_t data[xq::net::UDP_RBUF_SIZE];


        explicit Segment(UdpSession* sess, const sockaddr* name = nullptr, socklen_t namelen = sizeof(sockaddr), const uint8_t* data = nullptr, int datalen = 0)
            : namelen(namelen)
            , datalen(datalen)
            , time_ms(0)
            , sess(sess)
            , name({0,{0}}) {
            if (name) {
                ::memcpy(&this->name, name, namelen);
            }

            if (data && datalen > 0) {
                ASSERT(datalen < xq::net::UDP_RBUF_SIZE);
                ::memcpy(this->data, data, datalen);
            }
            else {
                ::memset(this->data, 0, xq::net::UDP_RBUF_SIZE);
            }
        }


        void set_name(const std::string& remote) {
            ASSERT(xq::net::str2addr(remote, &name, (socklen_t*)&namelen));
        }


        void set_name(const sockaddr *addr, socklen_t addrlen) {
            ::memcpy(&name, addr, addrlen);
            namelen = addrlen;
        }


        void set_data(const uint8_t* data, int datalen) {
            ASSERT(data && datalen > 0 && datalen <= xq::net::UDP_RBUF_SIZE);
            ::memcpy(this->data, data, datalen);
            this->datalen = datalen;
        }


        std::string to_string() const {
            char buf[xq::net::UDP_HEAD_SIZE * 2 + 500] = {0};
            sprintf(buf, "[%s]:[%s]", net::addr2str(&this->name).c_str(), xq::tools::bin2hex(this->data, this->datalen).c_str());
            return buf;
        }


        Segment(const Segment&) = delete;
        Segment(const Segment&&) = delete;
        Segment& operator=(const Segment&) = delete;
        Segment& operator=(const Segment&&) = delete;
    };
    // ------------------------------------------------------------------- END Segment -------------------------------------------------------------------


    typedef std::shared_ptr<UdpSession> Ptr;
    typedef int (*RcvCallback)(const Segment*);


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


    void close() {
        if (sockfd_ != INVALID_SOCKET) {
            xq::net::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }
    }


    void run(RcvCallback rcv_cb) {
        thread_ = std::thread(std::bind(&UdpSession::start_rcv, this, rcv_cb));
    }


    void stop() {
#ifndef WIN32
        constexpr char buf[1] = { 'X' };

        if (wp_ != -1) {
            ASSERT(::write(wp_, buf, 1) == 1);
            ::close(wp_);
            wp_ = -1;
        }
#endif // WIN32
        close();
    }


    void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }


#ifdef WIN32
    void start_rcv(RcvCallback rcv_cb) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(rcv_cb && "rcv_cb cannot be null");

        while (1) {
            Segment* seg = new Segment(this);
            int n = ::recvfrom(sockfd_, (char*)seg->data, UDP_RBUF_SIZE, 0, &seg->name, &seg->namelen);
            if (n < 0 && error() != 10060) {
                break;
            }

            seg->datalen = n;
            seg->time_ms = xq::tools::now_ms();
            n = rcv_cb(seg);
            if (n <= 0) {
                delete seg;
                if (n < 0) {
                    break;
                }
            }
        }

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            delete* itr;
            snd_buf_.erase(itr);
        }
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            Segment* seg = *itr;
            int n = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
            delete seg;
            snd_buf_.erase(itr++);
            if (n < 0) {
                // TODO: error handle
            }
        }

        return 0;
    }
#else

    void start_rcv(RcvCallback rcv_cb) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(rcv_cb && "rcv_cb cannot be null");

        int p[2];
        ASSERT(!pipe(p));
        int rp = p[0];
        wp_ = p[1];

        mmsghdr msgs[IO_RMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_RMSG_SIZE];
        Segment* segs[IO_RMSG_SIZE] = {nullptr};
        Segment* seg;
        msghdr* hdr;

        int n = IO_RMSG_SIZE, err;

        fd_set fds, rfds;
        FD_ZERO(&fds);
        FD_SET(sockfd_, &fds);
        FD_SET(rp, &fds);

        while(1) {
            for (int i = 0; i < n; i++) {
                if (!segs[i]) {
                    segs[i] = new Segment(this);
                }
                seg = segs[i];

                hdr = &msgs[i].msg_hdr;
                hdr->msg_name = &seg->name;
                hdr->msg_namelen = seg->namelen;
                hdr->msg_iov = &iovecs[i];
                hdr->msg_iovlen = 1;
                iovecs[i].iov_base = seg->data;
                iovecs[i].iov_len = UDP_RBUF_SIZE;
            }

            rfds = fds;
            n = ::select(FD_SETSIZE, &rfds, nullptr, nullptr, nullptr);
            if (n < 1) {
                continue;
            }

            if (FD_ISSET(rp, &rfds)) {
                char buf[1];
                ASSERT(::read(rp, buf, 1) == 1 && buf[0] == 'X');
                break;
            }

            n = ::recvmmsg(sockfd_, msgs, IO_RMSG_SIZE, MSG_WAITFORONE, nullptr);

            do {
                if (n < 0) {
                    err = error();
                    if (err != EAGAIN && err != EINTR) {
                        // TODO: error
                    }
                    break;
                }

                if (n == 0) {
                    break;
                }

                int64_t now_ms = xq::tools::now_ms();

                for (int i = 0; i < n; i++) {
                    seg = segs[i];
                    seg->sess = this;
                    seg->datalen = msgs[i].msg_len;
                    seg->time_ms = now_ms;

                    int res = rcv_cb(seg);
                    segs[i] = nullptr;

                    if (res <= 0) {
                        delete seg;
                        if (res < 0) {
                            break;
                        }
                    }
                }
            } while(0);
        }

        for (int i = 0; i < IO_RMSG_SIZE; i++) {
            if (segs[i]) {
                delete segs[i];
            }
        }

        while (!snd_buf_.empty()) {
            auto itr = snd_buf_.begin();
            delete* itr;
            snd_buf_.erase(itr);
        }

        ::close(rp);
    }


    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        mmsghdr msgs[IO_SMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_SMSG_SIZE];
        msghdr *hdr;
        Segment* seg;

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
    /// @param seg   需要发送的UdpSession::Segment
    /// @param force 立即发送数据
    /// 
    /// @return 成功返回 0, 否则返回 -1
    ///
    int send(const Segment* seg, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        if (force) {
            int ret = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
            delete seg;
            return ret;
        }
        snd_buf_.emplace_back(const_cast<Segment*>(seg));
        return 0;
    }


    int send(const uint8_t* data, size_t datalen, const sockaddr* remote, socklen_t remotelen, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        if (force) {
            
            return ::sendto(sockfd_, (const char*)data, datalen, 0, remote, remotelen);
        }

        Segment* seg = new Segment(this, remote, remotelen, data, datalen);
        return this->send(seg);
    }


private:
    UdpSession(SOCKET sockfd, const sockaddr *addr, socklen_t addrlen)
        : sockfd_(sockfd)
#ifndef WIN32
        , wp_(-1)
#endif // !WIN32
        , addr_({ 0, {0} })
        , addrlen_(sizeof(addr_)) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    SOCKET sockfd_;
#ifndef WIN32
    int wp_;
#endif // WIN32
    sockaddr addr_;
    socklen_t addrlen_;
    std::thread thread_;
    std::list<Segment*> snd_buf_;



    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_UDP_SESSION__
