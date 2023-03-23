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


        Segment()
            : namelen(sizeof(sockaddr)) {
            ::memset((uint8_t*)this + offsetof(Segment, datalen), 0, sizeof(Segment) - offsetof(Segment, datalen));
        }


        Segment(const sockaddr *name, socklen_t namelen) {
            ::memcpy(&name, name, namelen);
            this->namelen = namelen;
        }


        void set_name(const std::string& remote) {
            assert(xq::net::str2addr(remote, &name, (socklen_t*)&namelen));
        }


        void set_name(const sockaddr *addr, socklen_t addrlen) {
            ::memcpy(&name, addr, addrlen);
            namelen = addrlen;
        }


        void set_data(const uint8_t* data, int datalen) {
            assert(data && datalen > 0 && datalen <= xq::net::UDP_RBUF_SIZE);
            ::memcpy(this->data, data, datalen);
            this->datalen = datalen;
        }
    };
    // ------------------------------------------------------------------- END Segment -------------------------------------------------------------------


    typedef std::shared_ptr<UdpSession> Ptr;
    typedef int (*RcvCallback)(Segment*);


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
        if (sockfd_ != INVALID_SOCKET) {
            xq::net::close(sockfd_);
        }
    }


    void close() {
        xq::net::close(sockfd_);
        sockfd_ = INVALID_SOCKET;
    }


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
    int send(Segment* seg, bool force = false) {
        if (force) {
            assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
            int ret = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
            delete seg;
            return ret;
        }
        snd_buf_.emplace_back(seg);
        return 0;
    }


#ifdef WIN32
    void start_rcv(RcvCallback rcv_cb) {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        assert(rcv_cb && "rcv_cb cannot be null");

        

        while (1) {
            Segment* seg = new Segment;
            int n = ::recvfrom(sockfd_, (char*)seg->data, UDP_RBUF_SIZE, 0, &seg->name, &seg->namelen);
            if (n < 0 && error() != 10060) {
                break;
            }

            seg->datalen = n;
            seg->sess = this;
            seg->time_ms = xq::tools::now_ms();
            n = rcv_cb(seg);
            if (n <= 0) {
                delete seg;
                if (n < 0) {
                    break;
                }
            }
        }
    }


    int flush() {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        if (!snd_buf_.empty()) {
            for (auto itr = snd_buf_.begin(); itr != snd_buf_.end();) {
                Segment* seg = *itr;
                int n = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
                delete seg;
                snd_buf_.erase(itr++);
                if (n < 0) {
                    return n;
                }
            }
        }

        return 0;
    }
#else

    void start_rcv(RcvCallback rcv_cb) {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        assert(rcv_cb && "rcv_cb cannot be null");

        mmsghdr msgs[IO_RMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_RMSG_SIZE];
        Segment* segs[IO_RMSG_SIZE] = {nullptr};
        Segment* seg;
        msghdr* hdr;

        int n = IO_RMSG_SIZE, err;

        while(1) {
            for (int i = 0; i < n; i++) {
                if (!segs[i]) {
                    segs[i] = new Segment;
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
    }


    int flush() {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");

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


private:
    UdpSession(SOCKET sockfd, const sockaddr *addr, socklen_t addrlen)
        : sockfd_(sockfd)
        , addr_({ 0, {0} })
        , addrlen_(sizeof(addr_)) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    SOCKET sockfd_;
    sockaddr addr_;
    socklen_t addrlen_;
    std::list<Segment*> snd_buf_;


    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_UDP_SESSION__
