#ifndef __KCP_CONN__
#define __KCP_CONN__


#include "xq/tools/tools.hpp"
#include "xq/net/kcp.hpp"
#include "xq/net/net.hpp"
#include "xq/third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


class KcpHost final {
public:
    struct Seg {
        typedef moodycamel::BlockingConcurrentQueue<Seg*> Queue;

        static xq::tools::ObjectPool<Seg>* pool() {
            return xq::tools::ObjectPool<Seg>::Instance();
        }

        int len;            // 消息总长度
        KcpHost* host;      // 消息来源
        socklen_t addrlen;  // 地址长度
        sockaddr addr;      // 地址
        int64_t time_ms;    // 消息包时间
#ifndef WIN32
        uint8_t data[IO_BLOCK_SIZE][KCP_MTU];   // 数据块
#else
        uint8_t data[1][KCP_MTU];               // 数据块
#endif // !WIN32

/// <summary>
/// 构造函数
/// </summary>
        explicit Seg()
            : len(KCP_MTU* IO_BLOCK_SIZE)
            , host(nullptr)
            , addrlen(sizeof(addr))
            , addr({ 0,{0} })
            , time_ms(0) {
            assert(data);
        }
    }; // struct RxSeg;


private:
    friend class KcpConn;
    typedef xq::tools::SpinLock LockType;

    explicit KcpHost(uint32_t conv, const std::string& host, uint32_t que_num)
        : kcp_(new Kcp(conv, this))
        , raddr_({ 0, {0} })
        , raddrlen_(sizeof(raddr_))
        , que_num_(que_num)
        , time_ms_(xq::tools::now_milli())
        , last_ms_(0)
        , host_(host) {
        assert(kcp_);
        assert(xq::net::str2addr(host, &raddr_, &raddrlen_));
    }

    int _send(const uint8_t* data, size_t datalen) {
        std::lock_guard<LockType > lk(kcp_mtx_);
        return kcp_->send(data, datalen);
    }

    int _input(const uint8_t* data, long size) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->input(data, size);
    }

    int _recv(uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }

    void _update(int64_t now_ms) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        kcp_->update((uint32_t)(now_ms - time_ms_));
        last_ms_ = now_ms;
    }

    uint32_t get_conv() const {
        return kcp_->get_conv();
    }

    const std::string& get_host() const {
        return host_;
    }

    uint32_t get_que_num() const {
        return que_num_;
    }

    SOCKET ufd_;
    Kcp* kcp_;
    sockaddr raddr_;
    socklen_t raddrlen_;
    uint32_t que_num_;
    int64_t time_ms_;
    int64_t last_ms_;

    std::string host_;
    LockType kcp_mtx_;
}; // class KcpHost;


















class KcpConn final {
public:
    typedef std::shared_ptr<KcpConn> Ptr;

    static Ptr create(uint32_t conv, const std::string& local, const std::vector<std::string>& hosts, size_t nthread = 1) {
        assert(conv > 0 && conv != ~0);
        assert(hosts.size() > 0);
        return Ptr(new KcpConn(conv, local, hosts, nthread));
    }

    ~KcpConn() {
        for (auto que : rques_) {
            delete que;
        }

        for (auto kh : kcp_hosts_) {
            delete kh.second;
        }
    }

    void run() {
        ufd_ = udp_bind(local_);
        assert(ufd_ != INVALID_SOCKET && "udp socket build failed");
#ifdef WIN32
        constexpr int TIMEOUT = 5000;
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TIMEOUT, sizeof(TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        for (size_t i = 0, n = rques_.size(); i < n; i++) {
            kp_thread_pool_.emplace_back(std::bind(&KcpConn::_kcp_proc, this, rques_[i]));
        }

        update_thread_ = std::thread(std::bind(&KcpConn::_update, this));
        rx_thread_ = std::thread(std::bind(&KcpConn::_rx, this));

        rx_thread_.join();
        update_thread_.join();
        for (auto& t : kp_thread_pool_) {
            t.join();
        }

        kp_thread_pool_.clear();
        close(ufd_);
        ufd_ = INVALID_SOCKET;

        state_ = State::Stopped;
    }

    void stop() {
        state_ = State::Stopping;
    }

private:
    KcpConn(uint32_t conv, const std::string &local, const std::vector<std::string> &hosts, size_t nthread)
        : state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , conv_(conv)
        , local_(local) {

        size_t i, n = hosts.size();

        for (size_t i = 0; i < nthread; i++) {
            rques_.emplace_back(new KcpHost::Seg::Queue());
        }

        for (i = 0; i < n; i++) {
            KcpHost* kh = new KcpHost(conv_, hosts[i], i % nthread);
            kcp_hosts_.insert(std::make_pair(kh->get_host(), kh));
        }
    }

#ifdef WIN32
    static int output(const char* raw, int rawlen, IKCPCB*, void* user) {
        KcpHost* host = (KcpHost*)user;
        int n = ::sendto(host->ufd_, raw, rawlen, 0, &host->raddr_, host->raddrlen_);
        if (n < 0) {
            std::printf("send failed: %d\n", error());
        }

        return n;
    }

    void _rx() {
        int rawlen;
        uint32_t conv;
        KcpHost::Seg* seg;
        int64_t now_ms;
        KcpHost* kh;

        while (state_ == State::Running) {
            seg = KcpHost::Seg::pool()->get();
            seg->addrlen = sizeof(sockaddr);

            rawlen = ::recvfrom(ufd_, (char*)seg->data[0], KCP_MTU, 0, &seg->addr, &seg->addrlen);
            if (rawlen < 0 && error() != 10060) {
                // TODO: ...
                continue;
            }

            std::string host = xq::net::addr2str(&seg->addr);
            auto itr = kcp_hosts_.find(host);
            if (itr == kcp_hosts_.end()) {
                continue;
            }

            seg->host = kh = itr->second;
            rques_[kh->get_que_num()]->enqueue(seg);
        }
    }
#else
#endif // WIN32

    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(KCP_UPDATE_MS);

        int64_t now_ms;

        while (state_ == State::Running) {
            std::this_thread::sleep_for(INTVAL);
            now_ms = xq::tools::now_milli();
            for (auto& kh : kcp_hosts_) {
                kh.second->_update(now_ms);
            }
        }
    }

    void _kcp_proc(KcpHost::Seg::Queue *que) {
        constexpr std::chrono::duration TIMEOUT = std::chrono::seconds(5);

        KcpHost::Seg* seg;
        KcpHost* host;

        int n;
        size_t nleft, i;
        uint8_t* rbuf = new uint8_t[KCP_MAX_DATA_SIZE], * raw;

        while (state_ == State::Running) {
            if (que->wait_dequeue_timed(seg, TIMEOUT)) {
                do {
                    host = seg->host;

                    nleft = seg->len;
                    i = 0;

                    while (nleft > 0) {
                        n = nleft > KCP_MTU ? KCP_MTU : nleft;
                        raw = seg->data[i++];
                        if (host->_input(raw, n) < 0) {
                            // TODO:
                            break;
                        }
                        nleft -= n;
                    }

                    while (true) {
                        n = host->_recv(rbuf, KCP_MAX_DATA_SIZE);
                        if (n < 0) {
                            break;
                        }

                        assert(host->host_ == addr2str(&seg->addr));
                        // TODO: event:
                    }

                } while (0);
            }
        } // while (state_ == State::Running);
    }

    State state_;

    SOCKET ufd_;
    uint32_t conv_;

    std::string local_;

    std::thread rx_thread_;
    std::thread update_thread_;
    std::vector<std::thread> kp_thread_pool_;
    std::vector<xq::net::KcpHost::Seg::Queue*> rques_;
    std::unordered_map<std::string, KcpHost*> kcp_hosts_;
}; // class KcpConn;


} // namespace net
} // namespace xq


#endif // !__KCP_CONN__
