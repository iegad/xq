#ifndef __XQ_NET_KCP_CONN__
#define __XQ_NET_KCP_CONN__


#include "xq/tools/tools.hpp"
#include "xq/net/kcp.hpp"
#include "xq/net/net.hpp"
#include "xq/third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


typedef xq::tools::SpinLock LockType;


template <class TEvent>
class KcpConn final {
public:
    typedef std::shared_ptr<KcpConn> Ptr;


// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  BEG Host +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

class Host final {
public:
    friend class KcpConn<TEvent>;


    Host(uint32_t conv, const std::string& host, KcpConn *conn, int (*output)(const uint8_t* buf, size_t len, void* user))
        : kcp_(new Kcp(conv, this, output))
        , time_ms_(xq::tools::now_milli())
        , raddr_({ 0, {0} })
        , raddrlen_(sizeof(raddr_))
        , conn_(conn)
        , remote_(host) {
        assert(kcp_);
        assert(xq::net::str2addr(host, &raddr_, &raddrlen_));
    }


    ~Host() {
        if (kcp_) {
            delete kcp_;
        }
    }


    int send(const uint8_t* data, size_t datalen) {
        std::lock_guard<LockType > lk(kcp_mtx_);
        if (kcp_->waitsnd() >= (int)KCP_WND) {
            return 1;
        }
        
        int n = kcp_->send(data, datalen);
        if (n == 0) {
            kcp_->flush();
        }
        return n;
    }


    int input(const uint8_t* data, long size, int64_t now_ms) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->input(data, size, now_ms - time_ms_);
    }


    int recv(uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }


    void update(int64_t now_ms) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        kcp_->update((uint32_t)(now_ms - time_ms_));
    }


    uint32_t conv() const {
        return kcp_->conv();
    }


    const std::string& remote() const {
        return remote_;
    }


private:
    Kcp*             kcp_;
    int64_t          time_ms_;
    sockaddr         raddr_;
    socklen_t        raddrlen_;
    KcpConn<TEvent>* conn_;
    std::string      remote_;
    LockType         kcp_mtx_;

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
}; // class KcpHost;

// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  END Host +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


public:
    static Ptr create(uint32_t conv, const std::string& local, const std::vector<std::string>& hosts) {
        assert(conv > 0 && conv != (uint32_t)(~0));
        assert(hosts.size() > 0);
        return Ptr(new KcpConn(conv, local, hosts));
    }


    ~KcpConn() {
#ifndef WIN32
        for (auto &msg: msgs_) {
            iovec *iov = msg.msg_hdr.msg_iov;
            delete[] (uint8_t*)iov[0].iov_base;
            delete[] iov;
        }
#endif

        for (auto kh : hosts_) {
            delete kh.second;
        }

        if (event_) {
            delete event_;
        }
    }


    void run() {
        // Step 1: 创建 udp 套接字
        ufd_ = udp_bind(local_);
        assert(ufd_ != INVALID_SOCKET && "udp socket build failed");
#ifdef WIN32
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&IO_TIMEOUT, sizeof(IO_TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        // Step 3: 开启 kcp update线程
        update_thread_ = std::thread(std::bind(&KcpConn::_update, this));

        // Step 4: 开启 IO read 线程
        _rx();

        // Step 5: 等待 update线程
        update_thread_.join();

        // Step 9: 关闭UDP
        ufd_ = INVALID_SOCKET;

        state_ = State::Stopped;
    }


    void stop() {
        close(ufd_);
        state_ = State::Stopping;
    }


    int send(const std::string& host, const uint8_t* data, size_t datalen) {
        assert(hosts_.count(host) == 1);
        return hosts_[host]->send(data, datalen);
    }


private:
    KcpConn(uint32_t conv, const std::string &local, const std::vector<std::string> &hosts)
        : state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , conv_(conv)
        , local_(local)
        , event_(new TEvent) {

        size_t i, n = hosts.size();

#ifndef WIN32
        for (i = 0; i < IO_MSG_SIZE; i++) {
            msgs_[i].msg_hdr.msg_control = nullptr;
            msgs_[i].msg_hdr.msg_controllen = 0;
            msgs_[i].msg_hdr.msg_flags = 0;
            msgs_[i].msg_hdr.msg_name = nullptr;
            msgs_[i].msg_hdr.msg_namelen = 0;
            iovec *iov = new iovec[1];
            iov[0].iov_base = new uint8_t[IO_RBUF_SIZE];
            iov[0].iov_len = IO_RBUF_SIZE;
            msgs_[i].msg_hdr.msg_iov = iov;
            msgs_[i].msg_hdr.msg_iovlen = 1;
            msgs_[i].msg_len = 0;
        }
#endif

        for (i = 0; i < n; i++) {
            Host* host = new Host(conv_, hosts[i], this, &KcpConn::output);
            hosts_.insert(std::make_pair(host->remote_, host));
        }
    }


    // ------------------------
    // 发送数据, 该方法只在Linux平台下有效
    // ------------------------
    void _sendmsgs() {
#ifndef WIN32
        int len = msgs_len_;
        if (len > 0) {
            int n = ::sendmmsg(ufd_, msgs_, len, 0);
            if (n < 0) {
                int err = error();
                std::printf("sendmmsg: err: %d\n", err);
            }
            msgs_len_ = 0;
        }
#endif // !WIN32
    }


#ifdef WIN32
    static int output(const uint8_t* raw, size_t rawlen, void* user) {
        Host* host = (Host*)user;
        int n = ::sendto(host->conn_->ufd_, (const char *)raw, (size_t)rawlen, 0, &host->raddr_, host->raddrlen_);
        if (n < 0) {
            std::printf("send failed: %d\n", error());
        }

        return n;
    }

    void _rx() {
        int       rawlen, err, n;
        uint32_t  conv;
        Host*     host;
        char      rbuf[IO_RBUF_SIZE];
        sockaddr  addr;
        socklen_t addrlen = sizeof(addr);
        uint8_t*  indata = new uint8_t[KCP_MAX_DATA_SIZE];

        while (state_ == State::Running) {
            ::memset(&addr, 0, addrlen);
            addrlen = sizeof(addr);

            rawlen = ::recvfrom(ufd_, rbuf, IO_RBUF_SIZE, 0, &addr, &addrlen);

            do {
                if (rawlen < 0) {
                    err = error();
                    if (err != 10060) {
                        event_->on_error(xq::net::ErrType::IO_RECV, err, nullptr);
                    }
                    break;
                }

                if (rawlen < KCP_HEAD_SIZE) {
                    event_->on_error(xq::net::ErrType::KCP_HEAD, EK_INVALID, &addr);
                    break;
                }

                conv = Kcp::get_conv(rbuf);
                if (conv != conv_) {
                    event_->on_error(xq::net::ErrType::KC_HOST, EK_CONV, &addr);
                    break;
                }

                std::string remote = xq::net::addr2str(&addr);
                assert(remote.size() > 0);

                if (hosts_.count(remote) == 0) {
                    event_->on_error(xq::net::ErrType::KC_HOST, EK_UNKNOWN_HOST, &remote);
                    break;
                }

                host = hosts_[remote];
                if (host->input((uint8_t*)rbuf, rawlen, xq::tools::now_milli())) {
                    continue;
                }

                while (n = host->recv(indata, KCP_MAX_DATA_SIZE), n > 0) {
                    event_->on_message(host, indata, n);
                }
            } while (0);
        }

        delete[] indata;
    }
#else
    // ------------------------
    // Linux KCP output
    //    将数据添加到mmsghdr缓冲区, 当mmsghdr缓冲区满时调用底层方法
    // ------------------------
    static int output(const uint8_t* raw, size_t len, void* user) {
        assert(len > 0 && (size_t)len <= KCP_MTU);

        Host*        host = (Host*)user;
        KcpConn* c  = host->conn_;
        size_t       i    = c->msgs_len_;
        msghdr*      msg  = &c->msgs_[i].msg_hdr;

        msg->msg_name           = &host->raddr_;
        msg->msg_namelen        = host->raddrlen_;
        msg->msg_iov[0].iov_len = len;

        ::memcpy(msg->msg_iov[0].iov_base, raw, len);

        if (++c->msgs_len_ == IO_MSG_SIZE) {
            c->_sendmsgs();
        }

        return 0;
    }


    // ------------------------
    // Linux IO 工作线程
    // ------------------------
    void _rx() {
        static timespec TIMEOUT   = {.tv_sec = IO_TIMEOUT / 1000, .tv_nsec = 0};

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        uint32_t conv;

        int      i, n = IO_MSG_SIZE;
        socklen_t addrlen;
        size_t   rawlen;
        int64_t  now_ms;

        mmsghdr* msg;
        msghdr*  hdr;
        Host* host;

        sockaddr addrs[IO_MSG_SIZE];
        uint8_t  rbufs[IO_MSG_SIZE][IO_RBUF_SIZE];
        iovec    iovecs[IO_MSG_SIZE];
        uint8_t* indata = new uint8_t[KCP_MAX_DATA_SIZE];

        while(state_ == State::Running) {

            // Step 1: 
            for (i = 0; i < n; i++) {
                hdr                = &msgs[i].msg_hdr;
                hdr->msg_name      = &addrs[i];
                hdr->msg_namelen   = sizeof(sockaddr);
                hdr->msg_iov       = &iovecs[i];
                hdr->msg_iovlen    = 1;
                iovecs[i].iov_base = rbufs[i];
                iovecs[i].iov_len  = IO_RBUF_SIZE;
            }

            host = nullptr;
            // Step 2: 获取消息
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, &TIMEOUT);

            do {
                if (n < 0) {
                    event_->on_error(xq::net::ErrType::IO_RECV, error(), nullptr);
                    break;
                }

                if (n == 0) {
                    break;
                }

                now_ms = xq::tools::now_milli();

                for (i = 0; i < n; i++) {
                    msg    = &msgs[i];
                    rawlen = msg->msg_len;
                    if (rawlen < KCP_HEAD_SIZE) {
                        event_->on_error(xq::net::ErrType::KCP_HEAD, EK_INVALID, &addrs[i]);
                        continue;
                    }

                    // Step 3: 获取 kcp conv
                    conv = Kcp::get_conv(rbufs[i]);
                    if (conv != conv_) {
                        event_->on_error(xq::net::ErrType::KL_INVALID_CONV, conv, &addrs[i]);
                        continue;
                    }


                    std::string remote = xq::net::addr2str(&addrs[i]);
                    assert(remote.size() > 0);
                    addrlen = msg->msg_hdr.msg_namelen;

                    if (event_->on_recv(rbufs[i], rawlen, &addrs[i], addrlen) < 0) {
                        continue;
                    }

                    // Step 4: 获取会话
                    if (hosts_.count(remote) == 0) {
                        event_->on_error(xq::net::ErrType::KC_HOST, EK_CONV, &addrs[i]);
                        break;
                    }

                    host = hosts_[remote];
                    if (host->input(rbufs[i], rawlen, now_ms)) {
                        continue;
                    }

                    while (n = host->recv(indata, KCP_MAX_DATA_SIZE), n > 0) {
                        event_->on_message(host, indata, n);
                    }
                } // for (i = 0; i < n; i++);
            } while(0);
        }

        delete[] indata;
    }
#endif // WIN32

    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(KCP_UPDATE_MS);

        int64_t now_ms;

        while (state_ == State::Running) {
            std::this_thread::sleep_for(INTVAL);
            now_ms = xq::tools::now_milli();
            for (auto& host : hosts_) {
                host.second->update(now_ms);
            }

            _sendmsgs();
        }
    }


    State                                  state_;
    SOCKET                                 ufd_;
    uint32_t                               conv_;
    std::string                            local_;
    std::thread                            update_thread_;
    std::unordered_map<std::string, Host*> hosts_;
    TEvent*                                event_;
#ifndef WIN32
    mmsghdr                                msgs_[IO_MSG_SIZE];
#endif // !WIN32
    std::atomic<int>                       msgs_len_;
}; // class KcpConn;


} // namespace net
} // namespace xq


#endif // !__XQ_NET_KCP_CONN__
