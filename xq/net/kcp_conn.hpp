#ifndef __KCP_CONN__
#define __KCP_CONN__


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

    explicit Host(uint32_t conv, const std::string& host, uint32_t que_num, KcpConn *conn)
        : kcp_(new Kcp(conv, this))
        , que_num_(que_num)
        , time_ms_(xq::tools::now_milli())
        , last_ms_(0)
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
        return kcp_->send(data, datalen);
    }


    int input(const uint8_t* data, long size) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->input(data, size);
    }


    int recv(uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }


    void update(int64_t now_ms) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        kcp_->update((uint32_t)(now_ms - time_ms_));
        last_ms_ = now_ms;
    }


    uint32_t get_conv() const {
        return kcp_->get_conv();
    }


    const std::string& get_remote() const {
        return remote_;
    }


private:
    Kcp*                 kcp_;
    uint32_t             que_num_;
    int64_t              time_ms_;
    std::atomic<int64_t> last_ms_;
    sockaddr             raddr_;
    socklen_t            raddrlen_;
    KcpConn<TEvent>*     conn_;
    std::string          remote_;
    LockType             kcp_mtx_;

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
}; // class KcpHost;

// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  END Host +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  BEG Seg +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct Seg {
    static xq::tools::ObjectPool<Seg>* pool() {
        return xq::tools::ObjectPool<Seg>::instance();
    }

    int       len;            // 消息总长度
    Host*     host;      // 消息来源
    socklen_t addrlen;  // 地址长度
    sockaddr  addr;      // 地址
    int64_t   time_ms;    // 消息包时间
    uint8_t   data[IO_RBUF_SIZE];   // 数据块


    explicit Seg() {
        ::memset(this, 0, sizeof(*this));
        assert(data);
    }
}; // struct RxSeg;
// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  END Seg +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


private:
    typedef moodycamel::BlockingConcurrentQueue<Seg*> Queue;


public:
    static Ptr create(uint32_t conv, const std::string& local, const std::vector<std::string>& hosts, size_t nthread = 1) {
        assert(conv > 0 && conv != (uint32_t)(~0));
        assert(hosts.size() > 0);
        return Ptr(new KcpConn(conv, local, hosts, nthread));
    }


    ~KcpConn() {
#ifndef WIN32
        for (auto &msg: msgs_) {
            iovec *iov = msg.msg_hdr.msg_iov;
            delete[] (uint8_t*)iov[0].iov_base;
            delete[] iov;
        }
#endif

        for (auto que : rques_) {
            delete que;
        }

        for (auto kh : hosts_) {
            delete kh.second;
        }

        if (event_) {
            delete event_;
        }
    }


    void start() {
        // Step 1: 创建 udp 套接字
        ufd_ = udp_bind(local_);
        assert(ufd_ != INVALID_SOCKET && "udp socket build failed");
#ifdef WIN32
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&IO_TIMEOUT, sizeof(IO_TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        // Step 2: 开启工作线程
        for (Queue* q : rques_) {
            kp_thread_pool_.emplace_back(std::bind(&KcpConn::_kcp_proc, this, q));
        }

        // Step 3: 开启 kcp update线程
        update_thread_ = std::thread(std::bind(&KcpConn::_update, this));

        // Step 4: 开启 IO read 线程
        rx_thread_ = std::thread(std::bind(&KcpConn<TEvent>::_rx, this));

        // Step 5: 等待 IO 线程
        rx_thread_.join();

        // Step 6: 等待 update线程
        update_thread_.join();

        // Step 7: 等待工作线程
        for (auto& t : kp_thread_pool_) {
            t.join();
        }

        // Step 8: 清理工作线程
        kp_thread_pool_.clear();

        // Step 9: 清空Seg队列
        Seg* item[128];
        for (auto& q : rques_) {
            while (q->try_dequeue_bulk(item, 128));
        }

        // Step 10: 关闭UDP
        close(ufd_);
        ufd_ = INVALID_SOCKET;

        state_ = State::Stopped;
    }


    void stop() {
        state_ = State::Stopping;
    }


    int send(const std::string& host, const uint8_t* data, size_t datalen) {
        assert(hosts_.count(host) == 1);
        return hosts_[host]->send(data, datalen);
    }


private:
    KcpConn(uint32_t conv, const std::string &local, const std::vector<std::string> &hosts, size_t nthread)
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

        for (size_t i = 0; i < nthread; i++) {
            rques_.emplace_back(new Queue());
        }

        for (i = 0; i < n; i++) {
            Host* host = new Host(conv_, hosts[i], i % nthread, this);
            host->kcp_->set_output(&KcpConn::output);
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
    static int output(const char* raw, int rawlen, IKCPCB*, void* user) {
        Host* host = (Host*)user;
        int n = ::sendto(host->conn_->ufd_, raw, rawlen, 0, &host->raddr_, host->raddrlen_);
        if (n < 0) {
            std::printf("send failed: %d\n", error());
        }

        return n;
    }

    void _rx() {
        int      rawlen, err;
        uint32_t conv;
        Seg*     seg = nullptr;
        Host*    host;

        while (state_ == State::Running) {
            if (!seg) {
                seg = Seg::pool()->get();
            }
            assert(seg);
            seg->addrlen = sizeof(sockaddr);

            rawlen = ::recvfrom(ufd_, (char*)seg->data, IO_RBUF_SIZE, 0, &seg->addr, &seg->addrlen);

            do {
                if (rawlen < 0) {
                    err = error();
                    if (err != 10060) {
                        event_->on_error(xq::net::ErrType::IO_RECV, err, nullptr);
                    }
                    break;
                }

                if (rawlen < KCP_HEAD_SIZE) {
                    event_->on_error(xq::net::ErrType::KCP_HEAD, EK_INVALID, &seg->addr);
                    break;
                }

                conv = Kcp::get_conv(seg->data);
                if (conv != conv_) {
                    event_->on_error(xq::net::ErrType::KC_HOST, EK_CONV, &seg->addr);
                    break;
                }

                std::string remote = xq::net::addr2str(&seg->addr);
                assert(remote.size() > 0);

                if (hosts_.count(remote) == 0) {
                    event_->on_error(xq::net::ErrType::KC_HOST, EK_UNKNOWN_HOST, &remote);
                    break;
                }

                host         = hosts_[remote];
                seg->host    = host;
                seg->time_ms = xq::tools::now_milli();
                seg->len     = rawlen;

                rques_[host->que_num_]->enqueue(seg);
                seg = nullptr;
            } while (0);
        }
    }
#else
    // ------------------------
        // Linux KCP output
        //    将数据添加到mmsghdr缓冲区, 当mmsghdr缓冲区满时调用底层方法
        // ------------------------
        static int output(const char* raw, int len, IKCPCB*, void* user) {
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

                int      i,  n = IO_MSG_SIZE;
                size_t   rawlen;
                int64_t  now_ms;

                mmsghdr* msg;
                msghdr*  hdr;
                Host* host;

                Seg*     seg;
                Seg*     segs[IO_MSG_SIZE] = {nullptr};
                iovec    iovecs[IO_MSG_SIZE];

                while(state_ == State::Running) {

                    // Step 1: 申请 Seg
                    for (i = 0; i < n; i++) {
                        if (!segs[i]) {
                            segs[i] = Seg::pool()->get();
                        }

                        seg                = segs[i];
                        hdr                = &msgs[i].msg_hdr;
                        hdr->msg_name      = &seg->addr;
                        hdr->msg_namelen   = sizeof(sockaddr);
                        hdr->msg_iov       = &iovecs[i];
                        hdr->msg_iovlen    = 1;
                        iovecs[i].iov_base = seg->data;
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
                            seg    = segs[i];
                            rawlen = msg->msg_len;
                            if (rawlen < KCP_HEAD_SIZE) {
                                event_->on_error(xq::net::ErrType::KCP_HEAD, EK_INVALID, &seg->addr);
                                continue;
                            }

                            // Step 3: 获取 kcp conv
                            conv = Kcp::get_conv(seg->data);
                            if (conv != conv_) {
                                event_->on_error(xq::net::ErrType::KL_INVALID_CONV, conv, &seg->addr);
                                continue;
                            }


                            std::string remote = xq::net::addr2str(&seg->addr);
                            assert(remote.size() > 0);
                            seg->addrlen = msg->msg_hdr.msg_namelen;

                            if (event_->on_recv(seg->data, rawlen, &seg->addr, seg->addrlen) < 0) {
                                continue;
                            }

                            // Step 4: 获取会话
                            if (hosts_.count(remote) == 0) {
                                event_->on_error(xq::net::ErrType::KC_HOST, EK_CONV, &seg->addr);
                                break;
                            }

                            // Step 5: 构建Seg
                            host = hosts_[remote];
                            seg->host    = host;
                            seg->time_ms = now_ms;
                            seg->len     = rawlen;

                            // Step 6: 投递队列
                            assert(rques_[host->que_num_]->enqueue(seg));
                            segs[i] = nullptr;
                        } // for (i = 0; i < n; i++);
                    } while(0);
                }

                for (i = 0; i < IO_MSG_SIZE; i++) {
                    seg = segs[i];
                    if (seg) {
                        Seg::pool()->put(seg);
                    }
                }
            }
#endif // WIN32

    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(KCP_UPDATE_MS / 2);

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

    void _kcp_proc(Queue *que) {
        constexpr std::chrono::milliseconds TIMEOUT = std::chrono::milliseconds(IO_TIMEOUT);

        Seg*  seg;
        Host* host;

        int      nrecv;
        uint8_t* rbuf = new uint8_t[KCP_MAX_DATA_SIZE];

        while (state_ == State::Running) {
            if (que->wait_dequeue_timed(seg, TIMEOUT)) {
                do {
                    host = seg->host;
                    assert(host->remote_ == addr2str(&seg->addr));

                    nrecv = host->input(seg->data, seg->len);

                    while (true) {
                        nrecv = host->recv(rbuf, KCP_MAX_DATA_SIZE);
                        if (nrecv < 0) {
                            break;
                        }

                        if (event_->on_message(host, rbuf, nrecv) < 0) {
                            host->last_ms_ = 0;
                        }
                    }

                } while (0);
            }
        } // while (state_ == State::Running);
    }


    State                                  state_;
    SOCKET                                 ufd_;
    uint32_t                               conv_;
    std::string                            local_;
    std::thread                            rx_thread_;
    std::thread                            update_thread_;
    std::vector<std::thread>               kp_thread_pool_;
    std::vector<Queue*>                    rques_;
    std::unordered_map<std::string, Host*> hosts_;
    TEvent*                                event_;
#ifndef WIN32
    mmsghdr                                msgs_[IO_MSG_SIZE];
#endif // !WIN32
    std::atomic<int>                       msgs_len_;
}; // class KcpConn;


} // namespace net
} // namespace xq


#endif // !__KCP_CONN__
