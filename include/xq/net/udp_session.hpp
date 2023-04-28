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
template <class TEvent>
class UdpSession {
public:
    typedef std::shared_ptr<UdpSession> Ptr;


    static Ptr create(const std::string& laddr_str = "") {
        std::string lip = "0.0.0.0", lport = "0";

        if (laddr_str.size() > 0) {
            size_t npos = laddr_str.rfind(':');
            ASSERT(npos != std::string::npos);

            if (npos > 0) {
                lip = laddr_str.substr(0, npos);
            }

            lport = laddr_str.substr(npos + 1);
        }

        SOCKET sockfd = udp_bind(lip.c_str(), lport.c_str());
        return Ptr(new UdpSession(sockfd));
    }


    ~UdpSession() {
        close();
    }


    /* ----------------------------------------------------- */
    /// @brief 端口复用
    ///
    __inline__ void set_reuse() {
        constexpr int ON = 1;
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, (char *)&ON, sizeof(ON)));
#ifndef WIN32
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &ON, sizeof(ON)));
        ASSERT(!setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(ON)));
#endif // !WIN32
    }


    __inline__ void close() {
        stop();
    }


    /* ----------------------------------------------------- */
    /// @brief 异步开启 io loop
    ///
    __inline__ void run() {
        thread_ = std::thread(std::bind(&UdpSession::start_rcv, this));
    }


    /* ----------------------------------------------------- */
    /// @brief 停止 io loop
    ///
    __inline__ void stop() {
        if (sockfd_ != INVALID_SOCKET) {
            xq::net::close(sockfd_);
            sockfd_ = INVALID_SOCKET;
        }

        for (int i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
            snd_buf_[i] = nullptr;
        }
        nsnd_buf_ = 0;
    }


    /* ----------------------------------------------------- */
    /// @brief 等待 异步 io loop 完成
    ///
    __inline__ void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }


    __inline__ void rebuild(const std::string &laddr_str = "") {
        std::string lip = "0.0.0.0", lport = "0";

        if (laddr_str.size() > 0) {
            size_t npos = laddr_str.rfind(':');
            ASSERT(npos != std::string::npos);

            if (npos > 0) {
                lip = laddr_str.substr(0, npos);
            }

            lport = laddr_str.substr(npos + 1);
        }

        sockfd_ = udp_bind(lip.c_str(), lport.c_str());
        ASSERT(sockfd_ != INVALID_SOCKET);
    }


    __inline__ int join_multicast(const std::string& multi_local_ip, const std::string& multi_route_ip) {
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

        if (::setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq))) {
            return -1;
        }

        return 0;
    }


#ifdef WIN32
    /* ----------------------------------------------------- */
    /// @brief 开启同步 io loop
    ///
    void start_rcv() {
        ASSERT(sockfd_ != INVALID_SOCKET);

        int i, n, err;
        Datagram* dg = Datagram::get();

        while (sockfd_ != INVALID_SOCKET) {
            n = ::recvfrom(sockfd_, (char*)dg->data, UDP_DGX_SIZE, 0, &dg->name, &dg->namelen);
            if (n > 0) {
                dg->datalen = n;
                dg->time_us = xq::tools::now_us();
                n = ev_.on_recv(this, dg);
                if (n < 0) {
                    break;
                }
                else if (n > 0) {
                    dg = Datagram::get();
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
    }


    /* ----------------------------------------------------- */
    /// @brief 刷新写缓冲区
    ///
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


    int flush(const Datagram::ptr *dgs, int dglen) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(dgs && dglen > 0 && dglen <= IO_SMSG_SIZE);

        int n = 0, i;
        for (i = 0; i < dglen; i++) {
            Datagram* dg = dgs[i];
            if (n >= 0) {
                n = ::sendto(sockfd_, (char*)dg->data, dg->datalen, 0, &dg->name, dg->namelen);
            }
            Datagram::put(*(Datagram**)&dg);
        }

        return n >= 0 ? i : n;
    }


#else // !windows


    /* ----------------------------------------------------- */
    /// @brief 开启同步 io loop
    ///
    void start_rcv() {
        constexpr static timeval TIMEOUT{0, 500000};
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(!::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));

        mmsghdr msgs[IO_RMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_RMSG_SIZE];
        Datagram* dgs[IO_RMSG_SIZE] = {nullptr};
        Datagram* dg;
        msghdr* hdr;

        int i, n = IO_RMSG_SIZE, err, res;

        for (i = 0; i < n; i++) {
            if (!dgs[i]) {
                dgs[i] = Datagram::get();
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

        while(sockfd_ != INVALID_SOCKET) {
            n = ::recvmmsg(sockfd_, msgs, IO_RMSG_SIZE, MSG_WAITFORONE, nullptr);
            if (n < 0) {
                err = error();
                if (err != EAGAIN && err != EINTR) {
                    // TODO: error
                    break;
                }
                continue;
            }
            else if (n == 0) {
                continue;
            }

            int64_t now_us = xq::tools::now_us();

            for (i = 0; i < n; i++) {
                dg->datalen = msgs[i].msg_len;
                if (dg->datalen > UDP_DGX_SIZE) {
                    continue;
                }

                dg = dgs[i];
                dg->datalen = msgs[i].msg_len;
                dg->time_us = now_us;

                res = ev_.on_recv(this, dg);
                if (res < 0) {
                    break;
                }
                else if (res > 0) {
                    dg = dgs[i] = Datagram::get();
                    hdr = &msgs[i].msg_hdr;
                    hdr->msg_name = &dg->name;
                    hdr->msg_namelen = dg->namelen;
                    hdr->msg_iov = &iovecs[i];
                    hdr->msg_iovlen = 1;
                    iovecs[i].iov_base = dg->data;
                    iovecs[i].iov_len = UDP_DGX_SIZE;
                }
            }
        }

        for (i = 0; i < IO_RMSG_SIZE; i++) {
            if (dgs[i]) Datagram::put(dgs[i]);
        }

        for (i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }
        nsnd_buf_ = 0;
    }


    /* ----------------------------------------------------- */
    /// @brief 刷新写缓冲区
    ///
    int flush() {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");

        mmsghdr msgs[IO_SMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_SMSG_SIZE];
        msghdr *hdr;
        Datagram* dg;

        int res = 0, i;

        for (i = 0; i < nsnd_buf_; i++) {
            dg = snd_buf_[i];
            hdr = &msgs[i].msg_hdr;
            hdr->msg_name = &dg->name;
            hdr->msg_namelen = dg->namelen;
            hdr->msg_iov = &iovecs[i];
            hdr->msg_iovlen = 1;
            iovecs[i].iov_base = dg->data;
            iovecs[i].iov_len = dg->datalen;
        }

        if (res >= 0) {
            res = ::sendmmsg(sockfd_, msgs, nsnd_buf_, 0);
        }

        for (i = 0; i < nsnd_buf_; i++) {
            Datagram::put(snd_buf_[i]);
        }

        nsnd_buf_ = 0;
        return res;
    }


    int flush(const Datagram::ptr *dgs, int dglen) {
        mmsghdr msgs[IO_SMSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[IO_SMSG_SIZE];
        msghdr* hdr;
        Datagram* dg;

        int res = 0, i;

        for (i = 0; i < dglen; i++) {
            dg = dgs[i];
            hdr = &msgs[i].msg_hdr;
            hdr->msg_name = &dg->name;
            hdr->msg_namelen = dg->namelen;
            hdr->msg_iov = &iovecs[i];
            hdr->msg_iovlen = 1;
            iovecs[i].iov_base = dg->data;
            iovecs[i].iov_len = dg->datalen;
        }

        if (res >= 0) {
            res = ::sendmmsg(sockfd_, msgs, dglen, 0);
        }

        for (i = 0; i < dglen; i++) {
            Datagram::put(*(Datagram**)&dgs[i]);
        }

        return res;
    }
#endif // WIN32


    /* --------------------------------------------------------------------- */
    /// @brief 发送数据, 
    ///        参数dg 在主调函数中必需是 new 运算符创建. 
    ///        主调函数无需调用 delete 删除该对象, 该对象将由 该方法接管.
    ///        默认情况下, 当发送缓冲区满时, 该方法立即发送数据, 否则只会将数据提交至发送缓冲区.
    /// 
    /// @param dg    需要发送的UdpSession::Datagram
    /// @param force 立即发送数据
    /// 
    /// @return 返回0表示, 数据并未发送, 返回 大于0, 表示缓冲区中所有数据均以发送完毕, 小于0, 表示错误.
    ///
    __inline__ int send(const Datagram* dg, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET);
        ASSERT(dg);

        if (dg->datalen > 0) {   
            snd_buf_[nsnd_buf_++] = *(Datagram**)&dg;
            if (force || nsnd_buf_ == IO_SMSG_SIZE) {
                return flush();
            }
        }

        return 0;
    }


    /* --------------------------------------------------------------------- */
    /// @brief 发送数据, 该方法实际会调用send(const Datagram*, bool force)
    /// @param data 
    /// @param datalen 
    /// @param remote 
    /// @param remotelen 
    /// @param force 
    /// @return 
    __inline__ int send(const uint8_t* data, size_t datalen, const sockaddr* remote, socklen_t remotelen, bool force = false) {
        ASSERT(sockfd_ != INVALID_SOCKET && "udp session is invalid");
        ASSERT(data && datalen > 0);

        if (force) {
#ifndef WIN32
            return ::sendto(sockfd_, data, datalen, 0, remote, remotelen);
#else
            return ::sendto(sockfd_, (const char*)data, datalen, 0, remote, remotelen);
#endif // !WIN32
        }

        Datagram* dg = Datagram::get(remote, remotelen, data, datalen);
        return this->send(dg);
    }


private:
    UdpSession(SOCKET sockfd)
        : sockfd_(sockfd)
        , nsnd_buf_(0)
        , addr_({0, {0}})
        , addrlen_(sizeof(addr_)) {
        ASSERT(sockfd_ != INVALID_SOCKET);
        ::memset(snd_buf_, 0, sizeof(snd_buf_));
    }


    SOCKET sockfd_;

    /* --------------------------------------------------------------------- 
     * 在windows下, 该字段仅为 io loop的停止标识.
     * 在!windwos下, 该字段将是写管道的fd, 同时也是停止标识.
     * --------------------------------------------------------------------- */
    int nsnd_buf_;
    sockaddr addr_;
    socklen_t addrlen_;
    std::thread thread_;
    Datagram* snd_buf_[IO_SMSG_SIZE];
    TEvent ev_;

    UdpSession(const UdpSession&) = delete;
    UdpSession(const UdpSession&&) = delete;
    UdpSession& operator=(const UdpSession&) = delete;
    UdpSession& operator=(const UdpSession&&) = delete;
}; // class UdpSession;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_UDP_SESSION__
