#ifndef __XQ_NET_UDP_SESSION__
#define __XQ_NET_UDP_SESSION__


#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"


namespace xq {
namespace net {


class UdpSession {
public:
    // ------------------------------------------------------------------- BEG Segment -------------------------------------------------------------------
    // Udp 传输分组
    struct Segment {
        typedef std::shared_ptr<Segment> Ptr;


        static Ptr get() {
            return xq::tools::ObjectPool<Segment>::instance()->get();
        }

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


        void reset() {
            namelen = sizeof(sockaddr);
            ::memset((uint8_t*)this + offsetof(Segment, datalen), 0, sizeof(Segment) - offsetof(Segment, datalen));
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
    typedef int (*RcvCallback)(Segment::Ptr&);


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


    int send(Segment::Ptr seg, bool force = false) {
        if (force) {
            assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
            return ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
        }
        snd_buf_.emplace_back(seg);
        return 0;
    }


#ifdef WIN32
    void start_rcv(RcvCallback rcv_cb) {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        assert(rcv_cb && "rcv_cb cannot be null");

        while (1) {
            Segment::Ptr seg = Segment::get();
            assert(seg);
            seg->reset();

            int n = ::recvfrom(sockfd_, (char*)seg->data, UDP_RBUF_SIZE, 0, &seg->name, &seg->namelen);
            if (n < 0 && error() != 10060) {
                break;
            }

            seg->datalen = n;
            seg->sess = this;
            seg->time_ms = xq::tools::now_ms();
            if (rcv_cb(seg) < 0) {
                break;
            }
        }
    }


    int flush() {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        if (!snd_buf_.empty()) {
            for (auto& seg : snd_buf_) {
                int n = ::sendto(sockfd_, (char*)seg->data, seg->datalen, 0, &seg->name, seg->namelen);
                if (n < 0) {
                    return n;
                }
            }

            snd_buf_.clear();
        }

        return 0;
    }
#else

    void start_rcv(RcvCallback rcv_cb) {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        assert(rcv_cb && "rcv_cb cannot be null");

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_MSG_SIZE];
        Segment::Ptr segs[IO_MSG_SIZE] = {nullptr};
        Segment::Ptr seg;
        mmsghdr *msg;
        msghdr* hdr;

        int n = IO_MSG_SIZE, err;

        while(1) {
            for (int i = 0; i < n; i++) {
                segs[i] = Segment::get();
                seg = segs[i];
                seg->reset();

                hdr = &msgs[i].msg_hdr;
                hdr->msg_name = &seg->name;
                hdr->msg_namelen = seg->namelen;
                hdr->msg_iov = &iovecs[i];
                hdr->msg_iovlen = 1;
                iovecs[i].iov_base = seg->data;
                iovecs[i].iov_len = UDP_RBUF_SIZE;
            }

            n = ::recvmmsg(sockfd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, nullptr);
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
                    msg = &msgs[i];
                    seg = segs[i];
                    seg->sess = this;
                    seg->datalen = msg->msg_len;
                    seg->time_ms = now_ms;
                    rcv_cb(seg);
                }

            } while(0);
        }
    }


    int flush() {
        assert(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        size_t nleft = snd_buf_.size();
        int res = 0;

        if (nleft > 0) {
            size_t n, round = 0;

            mmsghdr msgs[IO_MSG_SIZE];
            ::memset(msgs, 0, sizeof(msgs));

            iovec iovecs[IO_MSG_SIZE];

            msghdr *hdr;
            Segment::Ptr seg;

            while (nleft > 0) {
                n = nleft > IO_MSG_SIZE ? IO_MSG_SIZE : nleft;
                for (size_t i = 0; i < n; i++) {
                    seg = snd_buf_[i + IO_MSG_SIZE * round];
                    hdr = &msgs[i].msg_hdr;
                    hdr->msg_name = &seg->name;
                    hdr->msg_namelen = seg->namelen;
                    hdr->msg_iov = &iovecs[i];
                    hdr->msg_iovlen = 1;
                    iovecs[i].iov_base = seg->data;
                    iovecs[i].iov_len = seg->datalen;
                }
                nleft -= n;
                round++;
            }

            res = ::sendmmsg(sockfd_, msgs, n, 0);
            snd_buf_.clear();
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
    std::vector<Segment::Ptr> snd_buf_;


    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_UDP_SESSION__
