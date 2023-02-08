#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__


#include <functional>
#include <unordered_set>


#include "xq/net/net.hpp"
#include "xq/net/kcp.hpp"
#include "xq/third/blockingconcurrentqueue.h"
#include "xq/tools/tools.hpp"


namespace xq {
namespace net {


// ------------------------------------------------------------------------ KcpSess ------------------------------------------------------------------------

/// @brief Kcp 会话
class KcpSess final {
public:


    // ------------------------------------ BEGIN Seg ------------------------------------
    struct Seg {

        /// @brief Seg 工作队列
        typedef moodycamel::BlockingConcurrentQueue<Seg*> Queue;

        /// @brief Seg 对象池
        static xq::tools::ObjectPool<Seg>* pool() {
            return xq::tools::ObjectPool<Seg>::Instance();
        }

        int       len;                          // 消息总长度
        KcpSess*  sess;                         // 消息来源
        socklen_t addrlen;                      // 地址长度
        sockaddr  addr;                         // 地址
        int64_t   time_ms;                      // 消息包时间
#ifndef WIN32
        uint8_t   data[IO_BLOCK_SIZE][KCP_MTU]; // 数据块
#else
        uint8_t   data[1][KCP_MTU];             // 数据块
#endif // !WIN32

        /// @brief Seg 构造函数
        explicit Seg()
            : len(KCP_MTU* IO_BLOCK_SIZE)
            , sess(nullptr)
            , addrlen(sizeof(addr))
            , addr({ 0,{0} })
            , time_ms(0) {
            assert(data);
        }
    }; // struct RxSeg;
    // ---------------------------------- END Seg --------------------------------------


    typedef xq::tools::SpinLock LockType;
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


    /// @brief 获取对端地址
    std::string get_remote() const {
        return remote_;
    }


    /// @brief 获取kcp conv 
    uint32_t get_conv() const {
        return kcp_->get_conv();
    }


    /// @brief  获取kcp 工作队列号
    uint32_t get_que_num() const {
        return que_num_;
    }


    /// @brief 获取最后接收数据时间
    int64_t get_last_time() {
        std::lock_guard<LockType> lk(time_mtx_);
        return last_ms_;
    }


    /// @brief  发送数据
    /// @return 成功返回0, 否则返回!0.
    int send(const uint8_t* buf, int len) {
        int rzt;
        {
            std::lock_guard<LockType> lk(kcp_mtx_);
            rzt = kcp_->send(buf, len);
        }
        
        if (rzt == 0) {
            _sendmsg();
        }

        return rzt;
    }


private:


    /// @brief 构造函数
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


    /// @brief 设置 kcp sess工作队列号
    void _set_que_num(uint32_t que_num) {
        que_num_ = que_num;
    }


    /// @brief 检查该会话地址是否改变
    /// @return 返回 0 时, 未改变; 返回 1 时, 新连接; 返回 2 时, 重新连接.
    /// @note  通过检查ufd 和对端地址, 来确认该kcp sess是否产生新的连接, 如果产生新的连接重置 kcp
    int _check(SOCKET ufd, sockaddr *addr, socklen_t addrlen, int64_t now_ms) {
        int  res     = 0;
        bool changed = false;

        if (ufd != ufd_) {
            ufd_ = ufd;
        }

        {
            std::lock_guard<LockType> lk(addr_mtx_);
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
                std::lock_guard<LockType> lk(kcp_mtx_);
                kcp_->reset();
            }

            {
                std::lock_guard<LockType> lk(time_mtx_);
                if (now_ms - last_ms_  < KCP_TIMEOUT) {
                    res++;
                }
                last_ms_ = time_ms_ = now_ms;
            }
        }

        return res;
    }
 
    
    /// @brief kcp input
    int _input(const uint8_t* data, long size) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->input(data, size);
    }


    /// @brief kcp recv
    int _recv(uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }


    /// @brief kcp update
    /// @param now_ms 当前时间戳毫秒
    /// @return 成功UPDATE返回true, 否则返回false
    bool _update(int64_t now_ms) {
        {
            std::lock_guard<LockType> lk(time_mtx_);
            // 该kcp sess未激活
            if (ufd_ == INVALID_SOCKET || last_ms_ == 0) {
                return false;
            }

            // 该kcp sess超时
            if (now_ms - last_ms_ > KCP_TIMEOUT) {
                time_ms_ = last_ms_ = 0;
                que_num_ = ~0;
                return false;
            }
        }

        {
            std::lock_guard<LockType> lk(kcp_mtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
        }

        _sendmsg();

        return true;
    }


    /// @brief 设置最后数据读取时间
    /// @param now_ms 当前时间戳毫秒
    void _set_last_ms(int64_t now_ms) {
        std::lock_guard<LockType> lk(time_mtx_);
        last_ms_ = now_ms;
    }


#ifndef WIN32


    /// @brief 追加数据
    void _append_data(uint8_t *data, size_t len) {

        iovec* iov = &msg_.msg_iov[msg_.msg_iovlen++];
        ::memcpy((uint8_t*)iov->iov_base, data, len);
        iov->iov_len = len;

        if (msg_.msg_iovlen == IO_BLOCK_SIZE >> 1) {
            _sendmsg();
        }
    }


#endif // !WIN32


    /// @brief 发送数据
    void _sendmsg() {
#ifndef WIN32
        if (msg_.msg_iovlen == 0) {
            return;
        }

        {
            std::lock_guard<LockType> lk(addr_mtx_);
            if (::sendmsg(ufd_, &msg_, 0) < 0) {
                //TODO: ...
                printf("sendmsg failed: %d\n", error());
            }
        }

        msg_.msg_iovlen = 0;
#endif // !WIN32
    }

    SOCKET    ufd_;      // UDP
    uint32_t  que_num_;  // 当前工作队列号
    int64_t   time_ms_;  // 激活时间
    int64_t   last_ms_;  // 最后读取数据时间
    sockaddr  raddr_;    // 对端地址
    socklen_t raddrlen_;

#ifndef WIN32
    msghdr msg_;         // sendmsg 入参
#endif // !WIN32

    Kcp*        kcp_;      // KCP实例
    std::string remote_;   // 对端址地表达式

    LockType    kcp_mtx_;
    LockType    time_mtx_;
    LockType    addr_mtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;


// ------------------------------------------------------------------------ KcpListener ------------------------------------------------------------------------


/// @brief Kcp服务端
class KcpListener final {
public:
    typedef std::shared_ptr<KcpListener> Ptr;


    // ----------------------------------- BEGIN IEvent -------------------------------------
    class IEvent {
    public:
        IEvent() = default;
        IEvent(const IEvent&) = delete;
        IEvent& operator=(const IEvent&) = delete;

        virtual int  on_connected(KcpSess*) { return 0; }
        virtual int  on_reconnected(KcpSess*) { return 0; }
        virtual void on_disconnected(KcpSess*) {}
        virtual int  on_message(KcpSess*, const uint8_t*, size_t) = 0;
        virtual void on_error(int, KcpSess*) {}
    }; // class IEvent;
    // ----------------------------------- END IEvent -------------------------------------


    /// @brief 创建KcpListener
    /// @param ev IEvent实例
    /// @param host 监听地址
    /// @param max_conn 最大连接数
    static Ptr create(IEvent *ev, const std::string& host, uint32_t max_conn) {
        assert(ev && "IEvent is invalid");
        assert(host.size() > 0 && "host is invalid");
        assert(max_conn > 0 && "max_conn is invalid");
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


    /// @brief 启动服务
    void run() {
        // Step 1: 创建 udp 监听套接字
        ufd_ = udp_bind(host_);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");
#ifdef WIN32
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&IO_TIMEOUT, sizeof(IO_TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        // Step 2: 开启工作线程
        for (xq::net::KcpSess::Seg::Queue *q : rques_) {
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
        KcpSess::Seg* item[IO_BLOCK_SIZE];
        for (auto& que : rques_) {
            while (que->try_dequeue_bulk(item, IO_BLOCK_SIZE));
        }

        // Step 10: 关闭UDP监听
        close(ufd_);
        ufd_ = INVALID_SOCKET;

        state_ = State::Stopped;
    }


    /// @brief 停止服徊
    void stop() {
        state_ = State::Stopping;
    }


private:


    /// @brief 构造函数
    KcpListener(IEvent *ev, const std::string& host, uint32_t max_conn)
        : MAX_CONN(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , event_(ev) {
        assert(MAX_CONN > 0 && "max_conn is invalid");

        for (size_t i = 1; i <= MAX_CONN; i++) {
            KcpSess* sess = new KcpSess(i);
            sess->kcp_->set_output(&KcpListener::output);
            sess->kcp_->nodelay(1, KCP_UPDATE_MS, 4, 0);
            sessions_.insert(std::make_pair(i, sess));
        }

        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            rques_.push_back(new KcpSess::Seg::Queue(2048));
        }
    }


    /// @brief KCP output回调
    static int output(const char* raw, int len, IKCPCB*, void* user) {
        KcpSess* sess = (KcpSess*)user;
#ifndef WIN32
        sess->_append_data((uint8_t*)raw, len);
#else
        int n = ::sendto(sess->ufd_, raw, len, 0, &sess->raddr_, sess->raddrlen_);
        if (n < 0) {
            std::printf("::sendto failed: %d\n", error());
        }
#endif // !WIN32
        return 0;
    }


#ifdef WIN32


    /// @brief IO 工作线程
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();

        int           rawlen, n;
        uint32_t      conv;
        KcpSess::Seg* seg;
        KcpSess*      sess;
        int64_t       now_ms;

        while (state_ == State::Running) {
            // Step 2: 获取数据
            seg = KcpSess::Seg::pool()->get();
            seg->addrlen = sizeof(sockaddr);

            rawlen = ::recvfrom(ufd_, (char *)seg->data[0], KCP_MTU, 0, &seg->addr, &seg->addrlen);
            if (rawlen < 0) {
                if (error() != 10060) {
                    event_->on_error(error(), nullptr);
                }
                continue;
            }

            if (rawlen < KCP_HEAD_SIZE) {
                continue;
            }

            now_ms = xq::tools::now_milli();
            // Step 3: 构建对象
            conv = Kcp::get_conv(seg->data[0]);
            if (conv == 0 || conv > MAX_CONN) {
                continue;
            }

            seg->sess = sess = sessions_[conv];
            n = sess->_check(ufd_, &seg->addr, seg->addrlen, now_ms);
            switch(n) {
            // 新连接
            case 1: {
                if (event_->on_connected(sess) < 0) {
                    continue;
                }

                sess->_set_que_num(conns_++ % QUE_SIZE);
                active_convs_.insert(conv);
            } break;
            
            // 重连
            case 2: {
                if (event_->on_reconnected(sess) < 0) {
                    continue;
                }
            } break;
            } // switch(n);

            seg->time_ms = now_ms;
            // Step 4: 投递消息
            assert(rques_[sess->get_que_num()]->enqueue(seg));
        }
    }


#else


    /// @brief IO 工作线程
    void _rx() {
        const  size_t   QUE_SIZE = std::thread::hardware_concurrency();
        static timespec TIMEOUT  = {.tv_sec = IO_TIMEOUT / 1000, .tv_nsec = 0};

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        size_t        rawlen;
        mmsghdr*      msg;
        msghdr*       hdr;
        uint32_t      conv;
        KcpSess*      sess;
        iovec         iovecs[IO_MSG_SIZE][IO_BLOCK_SIZE];
        int           i, j, n = IO_MSG_SIZE, nc;
        KcpSess::Seg* seg;
        KcpSess::Seg* segs[IO_MSG_SIZE] = {nullptr};
        int64_t       now_ms;

        while(state_ == State::Running) {
            for (i = 0; i < n; i++) {
                segs[i] = seg = KcpSess::Seg::pool()->get();
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

                    seg->sess = sess = sessions_[conv];
                    nc = sess->_check(ufd_, &seg->addr, seg->addrlen, now_ms);
                    switch(nc) {
                    // 新连接
                    case 1: {
                        if (event_->on_connected(sess) < 0) {
                            continue;
                        }

                        sess->_set_que_num(conns_++ % QUE_SIZE);
                        active_convs_.insert(conv);
                    } break;

                    // 重连
                    case 2: {
                        if (event_->on_reconnected(sess) < 0) {
                            continue;
                        }
                    } break;

                    } // switch(n);

                    seg->time_ms = now_ms;
                    // Step 4: 投递队列
                    assert(rques_[sess->get_que_num()]->enqueue(seg));
                } // for (i = 0; i < n; i++);
            }
        }

        for (i = 0; i < IO_MSG_SIZE; i++) {
            seg = segs[i];
            if (seg) {
                KcpSess::Seg::pool()->put(seg);
            }
        }
    }


#endif // WIN32


    /// @brief KCP UPDATE线程
    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(KCP_UPDATE_MS);

        uint32_t              conv;
        int64_t               now_ms;
        std::vector<uint32_t> convs(MAX_CONN);
        size_t                i, n;
        KcpSess*              sess;

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


    /// @brief KCP消息处理线程
    void _kcp_proc(KcpSess::Seg::Queue *que) {
        constexpr std::chrono::milliseconds TIMEOUT = std::chrono::milliseconds(IO_TIMEOUT);

        int           nrecv;
        uint8_t*      rbuf = new uint8_t[KCP_MAX_DATA_SIZE], *raw;
        KcpSess::Seg* seg;
        KcpSess*      sess;
        size_t        n, nleft, i;

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
                        if (event_->on_message(sess, rbuf, nrecv) < 0) {
                            sess->_set_last_ms(0);
                        }
                    }
                } while (0);

                KcpSess::Seg::pool()->put(seg);
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

    std::vector<KcpSess::Seg::Queue*>      rques_;          // read data queue

    xq::tools::Set<uint32_t>               active_convs_;   // 当前在线程kcp客户端
    std::unordered_map<uint32_t, KcpSess*> sessions_;       // 会话

    IEvent*                                event_;          // 事件实例
}; // class KcpListener;


} // namespace net;
} // namespace xq;


#endif // __KCP_LISTENER__
