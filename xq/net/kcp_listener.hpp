#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__


#include <functional>
#include <unordered_set>


#include "net/net.hpp"
#include "net/kcp.hpp"
#include "third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


class KcpSess final {
public:
    friend class KcpListener;

    ~KcpSess() {
#ifndef WIN32
        for (size_t i = 0; i < IO_BLOCK_SIZE; i++) {
            delete[](uint8_t*)msg_.msg_iov[i].iov_base;
        }
        delete[] msg_.msg_iov;
#endif // !WIN32

        if (kcp_) {
            delete kcp_;
        }
    }

    std::string get_remote() const {
        return remote_;
    }

    SOCKET get_ufd() const {
        return ufd_;
    }

    uint32_t get_conv() const {
        return kcp_->get_conv();
    }

    uint32_t get_que_num() const {
        return que_num_;
    }

    int64_t get_last_time() const {
        return last_ms_;
    }

    int send(const uint8_t* buf, int len) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        int rzt = kcp_->send(buf, len);
        if (rzt == 0) {
            kcp_->flush();
            _sendmsg();
        }
        return rzt;
    }

private:
    explicit KcpSess(uint32_t conv)
        : ufd_(INVALID_SOCKET)
        , que_num_(~0)
        , time_ms_(0)
        , last_ms_(0)
        , raddr_({ 0,{0}})
        , raddrlen_(sizeof(raddr_))
        , kcp_(new Kcp(conv, this)) {
#ifndef WIN32
        ::memset(&msg_, 0, sizeof(msg_));
        iovec* tmp = new iovec[IO_BLOCK_SIZE];
        msg_.msg_iov = tmp;
        for (size_t i = 0; i < IO_BLOCK_SIZE; i++) {
            tmp[i].iov_base = new uint8_t[KCP_MTU];
            tmp[i].iov_len = KCP_MTU;
        }
#endif // !WIN32
    }

    void _set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        kcp_->set_output(output);
    }

    void _set_que_num(uint32_t qidx) {
        que_num_ = qidx;
    }

    /// <summary>
    /// 检查该会话地址是否改变.
    /// </summary>
    /// <param name="ufd"></param>
    /// <param name="addr"></param>
    /// <param name="addrlen"></param>
    /// <returns>返回 0 时, 未改变; 返回 1 时, 新连接; 返回 2 时, 重新连接</returns>
    int _check(SOCKET ufd, sockaddr *addr, socklen_t addrlen) {
        int res = 0;
        bool changed = false;

        if (ufd != ufd_) {
            ufd_ = ufd;
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
            changed = ::memcmp(addr, &raddr_, addrlen) || addrlen != raddrlen_;
            if (changed) {
                ::memcpy(&raddr_, addr, addrlen);
                raddrlen_ = addrlen;
                remote_ = xq::net::addr2str(&raddr_);

        #ifndef WIN32
                msg_.msg_iovlen = 0;
                msg_.msg_name = &raddr_;
                msg_.msg_namelen = raddrlen_;
        #endif // !WIN32

                res++;
            }
        }

        if (changed) {
            {
                std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
                kcp_->reset();
            }

            {
                std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
                if (last_ms_ > 0) {
                    res++;
                }
                time_ms_ = xq::tools::now_milli();
            }
        }

        return res;
    }

    void _set_nodelay(int nodelay, int interval, int resend, int nc) {
        kcp_->nodelay(nodelay, interval, resend, nc);
    }

    int _input(const uint8_t* data, long size) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        return kcp_->input(data, size);
    }

    int _recv(uint8_t* buf, int len) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }

    bool _update(int64_t now_ms) {
        {
            std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
            if (ufd_ == INVALID_SOCKET || last_ms_ == 0) {
                return false;
            }

            if (now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
                time_ms_ = last_ms_ = 0;
                que_num_ = ~0;
                return false;
            }
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
        }

        _sendmsg();

        return true;
    }

    void _set_last_ms(int64_t now_ms) {
        std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
        last_ms_ = now_ms;
    }

    void _append_data(uint8_t *data, size_t len) {
#ifndef WIN32
        iovec* iov = &msg_.msg_iov[msg_.msg_iovlen++];
        ::memcpy((uint8_t*)iov->iov_base, data, len);
        iov->iov_len = len;

        if (msg_.msg_iovlen == IO_BLOCK_SIZE >> 1) {
            _sendmsg();
        }
#else
        std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
        if (::sendto(ufd_, (const char*)data, len, 0, &raddr_, raddrlen_)) {
            printf("sendto failed: %d\n", error());
            // TODO: ...
        }
#endif // !WIN32
    }

    void _sendmsg() {
#ifndef WIN32
        if (msg_.msg_iovlen == 0) {
            return;
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
            if (::sendmsg(ufd_, &msg_, 0) < 0) {
                //TODO: ...
                printf("sendmsg failed: %d\n", error());
            }
        }

        msg_.msg_iovlen = 0;
#endif // !WIN32
    }

    SOCKET ufd_;

    uint32_t que_num_;
    int64_t time_ms_;
    int64_t last_ms_;
    sockaddr raddr_;
    socklen_t raddrlen_;

#ifndef WIN32
    msghdr msg_;
#endif // !WIN32

    Kcp* kcp_;
    std::string remote_;

    xq::tools::SpinLock kcp_mtx_;
    xq::tools::SpinLock time_mtx_;
    xq::tools::SpinLock addr_mtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;


/// <summary>
/// KcpListener: Kcp 服务端
/// </summary>
class KcpListener final {
public:
    typedef std::shared_ptr<KcpListener> Ptr;

    class IEvent {
    public:
        IEvent() = default;
        IEvent(const IEvent&) = delete;
        IEvent& operator=(const IEvent&) = delete;

        virtual int on_connected(KcpSess*) { return 0; }
        virtual int on_reconnected(KcpSess*) { return 0; }
        virtual void on_disconnected(KcpSess*) {}
        virtual int on_message(KcpSess*, const uint8_t*, size_t) = 0;
        virtual int on_error(int, KcpSess*) { return 0; }
    }; // class IEvent;

    /// <summary>
    /// 创建KcpListener
    /// </summary>
    /// <param name="host">监听地址</param>
    /// <param name="max_conn">最大连接数</param>
    /// <returns></returns>
    static Ptr create(IEvent *ev, const std::string& host, uint32_t max_conn) {
        return Ptr(new KcpListener(ev, host, max_conn));
    }

    ~KcpListener() {
        for (auto& itr : sessions_) {
            delete itr.second;
        }

        for (auto q : rques_) {
            delete q;
        }
    }

    /// <summary>
    /// 启动服务
    /// </summary>
    void run() {
        // Step 1: 创建 udp 监听套接字
        ufd_ = udp_bind(host_);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");
#ifdef WIN32
        constexpr int TIMEOUT = 5000;
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&TIMEOUT, sizeof(TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        // Step 2: 开启工作线程
        for (xq::net::RxQueue *q : rques_) {
            kp_thread_pool_.emplace_back(std::bind(&KcpListener::_kcp_proc, this, q));
        }

        // Step 3: 开启 kcp update线程
        update_thread_ = std::thread(std::bind(&KcpListener::_update, this));

        // Step 4 :开启 io read 线程
        rx_thread_ = std::thread(std::bind(&KcpListener::_rx, this));

        // Step 5: 等待IO线程
        rx_thread_.join();

        // Step 6: 等待update线程
        update_thread_.join();

        // Step 7: 等待工作线程
        for (auto &t : kp_thread_pool_) {
            t.join();
        }
        // Step 8: 清理工作线程
        kp_thread_pool_.clear();

        // Step 9: 清空RxSeg队列
        RxSeg* item[IO_BLOCK_SIZE];
        for (auto& que : rques_) {
            while (que->try_dequeue_bulk(item, IO_BLOCK_SIZE));
        }

        // Step 10: 关闭UDP监听
        close(ufd_);
        ufd_ = INVALID_SOCKET;

        state_ = State::Stopped;
    }

    /// <summary>
    /// 停止服务
    /// </summary>
    void stop() {
        state_ = State::Stopping;
    }

private:
    /// <summary>
    /// 构造函数
    /// </summary>
    /// <param name="host">监听地址</param>
    /// <param name="max_conn">最大连接数</param>
    KcpListener(IEvent *ev, const std::string& host, uint32_t max_conn)
        : MAX_CONN(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , event_(ev) {
        assert(MAX_CONN > 0 && "max_conn is invalid");

        for (size_t i = 1; i <= MAX_CONN; i++) {
            KcpSess* sess = new KcpSess(i);
            sess->_set_output(&KcpListener::output);
            sess->_set_nodelay(1, KCP_UPDATE_MS, 4, 0);
            sessions_.insert(std::make_pair(i, sess));
        }

        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            rques_.push_back(new RxQueue(2048));
        }
    }

    /// <summary>
    /// kcp output 回调.
    /// 将需要发送的数据投递到KcpSess的发送缓冲区.
    /// </summary>
    /// <param name="raw"></param>
    /// <param name="len"></param>
    /// <param name=""></param>
    /// <param name="user"></param>
    /// <returns></returns>
    static int output(const char* raw, int len, IKCPCB*, void* user) {
        KcpSess* sess = (KcpSess*)user;
        sess->_append_data((uint8_t *)raw, len);
        return 0;
    }

#ifdef WIN32

    /// <summary>
    /// windows 平台 IO 读线程
    /// </summary>
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();

        int rawlen, n;
        uint32_t conv;
        RxSeg *seg;
        KcpSess* sess;
        int64_t now_ms;

        while (state_ == State::Running) {
            // Step 2: 获取数据
            seg = RxSeg::pool()->get();
            seg->addrlen = sizeof(sockaddr);

            rawlen = ::recvfrom(ufd_, (char *)seg->data[0], KCP_MTU, 0, &seg->addr, &seg->addrlen);
            if (rawlen < 0 && error() != 10060) {
                if (event_ && event_->on_error(error(), nullptr) < 0) {
                    continue;
                }
            }

            seg->time_ms = xq::tools::now_milli();

            if (rawlen < KCP_HEAD_SIZE) {
                continue;
            }

            // Step 3: 构建对象
            conv = Kcp::get_conv(seg->data[0]);
            if (conv == 0 || conv > MAX_CONN) {
                continue;
            }

            sess = sessions_[conv];
            n = sess->_check(ufd_, &seg->addr, seg->addrlen);
            switch(n) {
            // 新连接
            case 1: {
                if (event_ && event_->on_connected(sess) < 0) {
                    continue;
                }

                sess->_set_que_num(conns_++ % QUE_SIZE);
                active_convs_.insert(conv);
            } break;
            
            // 重连
            case 2: {
                if (event_ && event_->on_reconnected(sess) < 0) {
                    continue;
                }
            } break;

            } // switch(n);

            // Step 4: 投递消息
            assert(rques_[sess->get_que_num()]->enqueue(seg));
        }
    }

#else
    /// <summary>
    /// 类Unix 平台 IO 读线程
    /// </summary>
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();
        static timespec TIMEOUT = {.tv_sec = 5, .tv_nsec = 0};

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        size_t rawlen;
        mmsghdr *msg;
        msghdr *hdr;

        uint32_t conv;
        KcpSess *sess;

        iovec iovecs[IO_MSG_SIZE][IO_BLOCK_SIZE];

        int i, j, n = IO_MSG_SIZE, nc;

        RxSeg *seg;
        RxSeg *segs[IO_MSG_SIZE] = {nullptr};

        int64_t now_ms;

        while(state_ == State::Running) {
            for (i = 0; i < n; i++) {
                segs[i] = seg = RxSeg::pool()->get();
                hdr = &msgs[i].msg_hdr;
                hdr->msg_name = &seg->addr;
                hdr->msg_namelen = sizeof(sockaddr);
                hdr->msg_iov = iovecs[i];
                hdr->msg_iovlen = IO_BLOCK_SIZE;

                for (j = 0; j < IO_BLOCK_SIZE; j++) {
                    iovecs[i][j].iov_base = seg->data[j];
                    iovecs[i][j].iov_len = KCP_MTU;
                }
            }

            // Step 2: 获取消息
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, &TIMEOUT);

            if (n > 0) {
                now_ms = xq::tools::now_milli();
                for (i = 0; i < n; i++) {
                    msg = &msgs[i];
                    seg = segs[i];
                    segs[i] = nullptr;
                    rawlen = msg->msg_len;
                    if (rawlen < KCP_HEAD_SIZE) {
                        continue;
                    }

                    seg->addrlen = msg->msg_hdr.msg_namelen;
                    seg->len = rawlen;

                    // Step 3: 获取会话
                    conv = Kcp::get_conv(seg->data[0]);
                    if (conv == 0 || conv > MAX_CONN) {
                        continue;
                    }

                    sess = sessions_[conv];
                    nc = sess->_check(ufd_, &seg->addr, seg->addrlen);
                    switch(nc) {
                    // 新连接
                    case 1: {
                        if (event_ && event_->on_connected(sess) < 0) {
                            continue;
                        }

                        sess->_set_que_num(conns_++ % QUE_SIZE);
                        active_convs_.insert(conv);
                    } break;

                    // 重连
                    case 2: {
                        if (event_ && event_->on_reconnected(sess) < 0) {
                            continue;
                        }
                    } break;

                    } // switch(n);
                    seg->sess = sess;
                    seg->time_ms = now_ms;

                    // Step 4: 投递队列
                    assert(rques_[sess->get_que_num()]->enqueue(seg));
                } // for (i = 0; i < n; i++);
            }
        }

        for (i = 0; i < IO_MSG_SIZE; i++) {
            seg = segs[i];
            if (seg) {
                RxSeg::pool()->put(seg);
            }
        }
    }
#endif // WIN32

    /// <summary>
    /// kcp update 线程
    /// </summary>
    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(KCP_UPDATE_MS);

        uint32_t conv;
        int64_t now_ms;
        std::vector<uint32_t> convs(MAX_CONN);
        size_t i, n;
        KcpSess* sess;

        while (state_ == State::Running) {
            std::this_thread::sleep_for(INTVAL);

            now_ms = xq::tools::now_milli();
            n = active_convs_.as_vec(convs);
            for (i = 0; i < n; i++) {
                conv = convs[i];
                sess = sessions_[conv];
                if (!sess->_update(now_ms)) {
                    active_convs_.erase(conv);
                    conns_--;
                    if (event_) {
                        event_->on_disconnected(sess);
                    }
                }
            }
        }
    }

    /// <summary>
    /// kcp 工作线程
    /// </summary>
    /// <param name="que">RxSeg队列</param>
    void _kcp_proc(RxQueue *que) {
        constexpr std::chrono::duration TIMEOUT = std::chrono::seconds(5);

        int nrecv;

        uint8_t* rbuf = new uint8_t[KCP_MAX_DATA_SIZE], *raw;

        RxSeg* seg;
        KcpSess* sess;
        size_t n, nleft, i;

        while (state_ == State::Running) {
            if (que->wait_dequeue_timed(seg, TIMEOUT)) {
                do {
                    // Step 1: 获取KcpSess
                    sess = seg->sess;

                    // Step 2: 获取KCP消息包
                    nleft = seg->len;
                    i = 0;

                    while (nleft > 0) {
                        n = nleft > KCP_MTU ? KCP_MTU : nleft;
                        raw = seg->data[i++];
                        if (sess->_input(raw, n) < 0) {
                            // TODO:
                            break;
                        }
                        nleft -= n;
                    }

                    while (true) {
                        // Step 3: 获取消息包
                        nrecv = sess->_recv(rbuf, KCP_MAX_DATA_SIZE);
                        if (nrecv < 0) {
                            break;
                        }

                        assert(sess->get_remote() == addr2str(&seg->addr));
                        if (event_->on_message(sess, rbuf, nrecv) == 0) {
                            sess->_set_last_ms(seg->time_ms);
                        }
                    }
                } while (0);

                RxSeg::pool()->put(seg);
            }
        } // while (state_ == State::Running);

        delete[] rbuf;
    }

    const uint32_t                         MAX_CONN;        // 最大连接数
    State                                  state_;          // 服务端状态

    SOCKET                                 ufd_;            // 监听套接字
    std::string                            host_;           // 监听地址
    std::atomic_int32_t                    conns_;          // 当前连接数

    std::thread                            rx_thread_;      // io read thread
    std::thread                            update_thread_;  // kcp update thread
    std::vector<std::thread>               kp_thread_pool_; // 工作线程池 kcp procedure thread pool

    std::vector<RxQueue*>                  rques_;          // read data queue

    xq::tools::Set<uint32_t>               active_convs_;   // 当前在线程kcp客户端
    std::unordered_map<uint32_t, KcpSess*> sessions_;       // 会话

    IEvent*                                event_;          // 事件实例
}; // class KcpListener;


} // namespace net;
} // namespace xq;


#endif // __KCP_LISTENER__
