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


typedef std::mutex LockType;


// ------------------------------------------------------------------------ KcpSess ------------------------------------------------------------------------

class KcpListener;


/// @brief Kcp 会话
class KcpSess final {
public:
    typedef KcpSess* ptr;


    friend class KcpListener;

    // ------------------------------------ BEGIN Seg ------------------------------------
    struct Seg {

        /// @brief Seg 工作队列
        typedef moodycamel::BlockingConcurrentQueue<Seg*> Queue;

        /// @brief Seg 对象池
        static xq::tools::ObjectPool<Seg>* pool() {
            return xq::tools::ObjectPool<Seg>::instance();
        }

        int       len;                               // 消息总长度
        KcpSess*  sess;                              // 消息来源
        socklen_t addrlen;                           // 地址长度
        sockaddr  addr;                              // 地址
        int64_t   time_ms;                           // 消息包时间
        uint8_t   data[IO_RBUF_SIZE]; // 数据块

        /// @brief Seg 构造函数
        explicit Seg()
            : len(0)
            , sess(nullptr)
            , addrlen(sizeof(addr))
            , addr({ 0,{0} })
            , time_ms(0) {
            assert(data);
        }
    }; // struct RxSeg;
    // ---------------------------------- END Seg --------------------------------------


    static xq::tools::ObjectPool<KcpSess>* pool() {
        return xq::tools::ObjectPool<KcpSess>::instance();
    }


    /// @brief 构造函数
    explicit KcpSess()
        : que_num_(~0)
        , time_ms_(0)
        , last_ms_(0)
        , raddr_({ 0,{0} })
        , raddrlen_(sizeof(raddr_))
        , listener_(nullptr)
        , kcp_(nullptr) {}


    ~KcpSess() {
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


    /// @brief  发送数据
    /// @return 成功返回0, 否则返回!0.
    int send(const uint8_t* buf, int len, bool force = false) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->send(buf, len, force);
    }


private:


    void _set(
        uint32_t conv, 
        KcpListener *listener, 
        sockaddr *addr, 
        socklen_t addrlen, 
        const std::string& remote, 
        int64_t now_ms, 
        uint32_t que_num, 
        int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {

        if (!kcp_) {
            kcp_ = new Kcp(conv, this);
            kcp_->set_output(output);
        } else {
            kcp_->reset(conv);
        }

        if (!listener_) {
            listener_ = listener;
        }

        ::memcpy(&raddr_, addr, addrlen);
        raddrlen_ = addrlen;
        remote_ = remote;
        que_num_ = que_num;
        time_ms_ = last_ms_ = now_ms;
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
    int _update(int64_t now_ms) {
        {
            // 该kcp sess超时
            if (now_ms - last_ms_ > KCP_TIMEOUT) {
                time_ms_ = last_ms_ = 0;
                return -1;
            }
        }

        {
            std::lock_guard<LockType> lk(kcp_mtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
        }

        return 0;
    }


    uint32_t  que_num_;  // 当前工作队列号
    int64_t   time_ms_;  // 激活时间
    std::atomic<int64_t>   last_ms_;  // 最后读取数据时间

    sockaddr  raddr_;    // 对端地址
    socklen_t raddrlen_;

    KcpListener* listener_;
    Kcp*         kcp_;      // KCP实例
    std::string  remote_;   // 对端址地表达式

    LockType     kcp_mtx_;  // kcp锁

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
        virtual void on_send(KcpSess*, const uint8_t*, size_t) {}
        virtual void on_recv(KcpSess*, KcpSess::Seg*) {}
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
#ifndef WIN32
        for (auto &msg: msgs_) {
            delete[] (uint8_t *)msg.msg_hdr.msg_iov[0].iov_base;
            delete [] msg.msg_hdr.msg_iov;
        }
#endif

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
        KcpSess::Seg* item[128];
        for (auto& que : rques_) {
            while (que->try_dequeue_bulk(item, 128));
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
        : MAX_CONV(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , event_(ev)
        , msgs_len_(0) {
#ifndef WIN32
        for (size_t i = 0; i < IO_MSG_SIZE; i++) {
            msgs_[i].msg_hdr.msg_control = nullptr;
            msgs_[i].msg_hdr.msg_controllen = 0;
            msgs_[i].msg_hdr.msg_flags = 0;
            msgs_[i].msg_hdr.msg_name = nullptr;
            msgs_[i].msg_hdr.msg_namelen = 0;
            iovec *iov = new iovec[1];
            iov[0].iov_base = new uint8_t[IO_RBUF_SIZE];
            iov[0].iov_len = IO_RBUF_SIZE;
            msgs_[i].msg_hdr.msg_iov = &iov[0];
            msgs_[i].msg_hdr.msg_iovlen = 1;
            msgs_[i].msg_len = 0;
        }
#endif
        assert(MAX_CONV > 0 && "max_conn is invalid");

        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            rques_.push_back(new KcpSess::Seg::Queue(2048));
        }
    }


    void _sendmsgs() {
#ifndef WIN32
        if (msgs_len_ > 0) {
            int n = ::sendmmsg(ufd_, msgs_, msgs_len_, 0);
            if (n < 0) {
                int err = error();
                std::printf("sendmmsg: err: %d\n", err);
            }
            msgs_len_ = 0;
        }
#endif // !WIN32
    }


#ifdef WIN32
    /// @brief KCP output回调
    static int output(const char* raw, int len, IKCPCB*, void* user) {
        KcpSess* sess = (KcpSess*)user;
        KcpListener* l = sess->listener_;
        int n = ::sendto(l->ufd_, raw, len, 0, &sess->raddr_, sess->raddrlen_);
        if (n < 0) {
            sess->listener_->event_->on_error(error(), sess);
            return -1;
        }
        sess->listener_->event_->on_send(sess, (const uint8_t *)raw, len);
        return 0;
    }

    /// @brief IO 工作线程
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();
        static const size_t MAX_COUNT = MAX_CONV * 5;
        using SegPool = xq::tools::ObjectPool<KcpSess::Seg>;

        SegPool*      segpool = KcpSess::Seg::pool();
        int           rawlen, n;
        uint32_t      conv;
        KcpSess::Seg* seg = nullptr;
        KcpSess*      sess;
        int64_t       now_ms;

        while (state_ == State::Running) {
            // Step 2: 获取数据
            if (!seg) {
                seg = segpool->get();
                assert(seg);
                seg->addrlen = sizeof(sockaddr);
            }

            rawlen = ::recvfrom(ufd_, (char *)seg->data[0], IO_RBUF_SIZE, 0, &seg->addr, &seg->addrlen);

            do {
                if (rawlen < 0) {
                    if (error() != 10060) {
                        event_->on_error(error(), nullptr);
                    }
                    break;
                }

                if (rawlen < KCP_HEAD_SIZE) {
                    break;
                }

                // Step 3: 构建对象
                conv = Kcp::get_conv(seg->data);
                if (conv == 0 || conv > MAX_CONV) {
                    break;
                }

                now_ms = xq::tools::now_milli();
                std::string remote = xq::net::addr2str(&seg->addr);
                assert(remote.size() > 0);

                if (!sessions_.get(remote, sess)) {
                    if (conns_ >= MAX_COUNT) {
                        continue;
                    }

                    sess = KcpSess::pool()->get();
                    sess->_set(conv, this, &seg->addr, seg->addrlen, remote, now_ms, conns_++ % QUE_SIZE, &KcpListener::output);
                    sessions_.insert(remote, sess);
                }

                seg->sess = sess;
                seg->time_ms = now_ms;
                seg->len = rawlen;
                event_->on_recv(sess, seg);
                // Step 4: 投递消息
                assert(rques_[sess->que_num_]->enqueue(seg));
                seg = nullptr;
            } while (0);
        }
    }


#else
    /// @brief KCP output回调
    static int output(const char* raw, int len, IKCPCB*, void* user) {
        KcpSess* sess = (KcpSess*)user;
        KcpListener *l = sess->listener_;
        size_t i = l->msgs_len_;
        msghdr *msg = &l->msgs_[i].msg_hdr;

        msg->msg_name = &sess->raddr_;
        msg->msg_namelen = sess->raddrlen_;
        ::memcpy(msg->msg_iov[0].iov_base, raw, len);
        msg->msg_iov[0].iov_len = len;
        if (++l->msgs_len_ == IO_MSG_SIZE) {
            l->_sendmsgs();
        }

        return 0;
    }


    /// @brief IO 工作线程
    void _rx() {
        const  size_t   QUE_SIZE = std::thread::hardware_concurrency();
        const int32_t MAX_CONN = MAX_CONV * 5;
        static timespec TIMEOUT  = {.tv_sec = IO_TIMEOUT / 1000, .tv_nsec = 0};

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        size_t        rawlen;
        mmsghdr*      msg;
        msghdr*       hdr;
        uint32_t      conv;
        KcpSess*      sess;
        iovec         iovecs[IO_MSG_SIZE];
        int           i,  n = IO_MSG_SIZE;
        KcpSess::Seg* seg;
        KcpSess::Seg* segs[IO_MSG_SIZE] = {nullptr};
        int64_t       now_ms;

        while(state_ == State::Running) {
            for (i = 0; i < n; i++) {
                if (!segs[i]) {
                    segs[i] = KcpSess::Seg::pool()->get();
                }
                seg = segs[i];
                hdr = &msgs[i].msg_hdr;
                hdr->msg_name = &seg->addr;
                hdr->msg_namelen = sizeof(sockaddr);
                hdr->msg_iov = &iovecs[i];
                hdr->msg_iovlen = 1;
                iovecs[i].iov_base = seg->data;
                iovecs[i].iov_len = IO_RBUF_SIZE;
            }

            // Step 2: 获取消息
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, &TIMEOUT);

            do {
                if (n < 0) {
                    std::printf("error: %d\n", error());
                    // TODO: error
                    break;
                }

                if (n == 0) {
                    break;
                }

                now_ms = xq::tools::now_milli();
                for (i = 0; i < n; i++) {
                    msg = &msgs[i];
                    seg = segs[i];
                    rawlen = msg->msg_len;
                    if (rawlen < KCP_HEAD_SIZE) {
                        continue;
                    }

                    // Step 3: 获取会话
                    conv = Kcp::get_conv(seg->data);
                    if (conv == 0 || conv > MAX_CONV) {
                        continue;
                    }

                    std::string remote = xq::net::addr2str(&seg->addr);
                    assert(remote.size() > 0);

                    seg->addrlen = msg->msg_hdr.msg_namelen;
                    if (!sessions_.get(remote, sess)) {
                        if (conns_ >= MAX_CONN) {
                            break;
                        }

                        sess = KcpSess::pool()->get();
                        sess->_set(conv, this, &seg->addr, seg->addrlen, remote, now_ms, (conns_ + 1) % QUE_SIZE, &KcpListener::output);

                        if (event_->on_connected(sess) < 0) {
                            continue;
                        }

                        conns_++;
                        sessions_.insert(remote, sess);
                    }

                    seg->sess = sess;
                    seg->time_ms = now_ms;
                    seg->len = rawlen;
                    event_->on_recv(sess, seg);
                    // Step 4: 投递队列
                    assert(rques_[sess->que_num_]->enqueue(seg));
                    segs[i] = nullptr;
                } // for (i = 0; i < n; i++);
            } while(0);
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
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(1);

        int64_t               now_ms;
        std::vector<KcpSess*> convs(MAX_CONV);
        std::vector<std::string> erase_keys(MAX_CONV);
        std::vector<KcpSess*> erase_sess(MAX_CONV);
        size_t                i, n, nerase;
        KcpSess*              sess;

        while (state_ == State::Running) {
            nerase = 0;
            std::this_thread::sleep_for(INTVAL);

            now_ms = xq::tools::now_milli();
            n = sessions_.get_all_vals(convs);
            for (i = 0; i < n; i++) {
                sess = convs[i];
                if (sess->_update(now_ms) < 0) {
                    erase_sess[nerase] = sess;
                    erase_keys[nerase++] = sess->remote_;
                }
            }

            _sendmsgs();

            sessions_.erase(erase_keys, nerase);
            for (i = 0; i < nerase; i++) {
                KcpSess::pool()->put(erase_sess[i]);
            }
        }
    }


    /// @brief KCP消息处理线程
    void _kcp_proc(KcpSess::Seg::Queue *que) {
        constexpr std::chrono::milliseconds TIMEOUT = std::chrono::milliseconds(IO_TIMEOUT);

        int           nrecv;
        uint8_t*      rbuf = new uint8_t[KCP_MAX_DATA_SIZE];
        KcpSess::Seg* seg;
        KcpSess*      sess;

        while (state_ == State::Running) {
            if (que->wait_dequeue_timed(seg, TIMEOUT)) {
                do {
                    // Step 1: 获取KcpSess
                    sess = seg->sess;
                    std::string tmp = addr2str(&seg->addr);
                    assert(sess->remote_ == addr2str(&seg->addr));

                    // Step 2: 获取KCP消息包
                    if (sess->_input(seg->data, seg->len) < 0) {
                        // TODO:
                        break;
                    }

                    while (true) {
                        // Step 3: 获取消息包
                        nrecv = sess->_recv(rbuf, KCP_MAX_DATA_SIZE);
                        if (nrecv < 0) {
                            break;
                        }

                        sess->last_ms_ = event_->on_message(sess, rbuf, nrecv) == 0 ? seg->time_ms : 0;
                    }
                } while (0);

                KcpSess::Seg::pool()->put(seg);
            }
        } // while (state_ == State::Running);

        delete[] rbuf;
    }


    const uint32_t                         MAX_CONV;        // 最大连接数
    State                                  state_;          // 服务端状态

    SOCKET                                 ufd_;            // 监听套接字
    std::string                            host_;           // 监听地址
    std::atomic_int32_t                    conns_;          // 当前连接数

    std::thread                            rx_thread_;      // io read thread
    std::thread                            update_thread_;  // kcp update thread
    std::vector<std::thread>               kp_thread_pool_; // 工作线程池 kcp procedure thread pool

    std::vector<KcpSess::Seg::Queue*>      rques_;          // read data queue

    xq::tools::Map<std::string, KcpSess*>  sessions_;       // 会话

    IEvent*                                event_;          // 事件实例

#ifndef WIN32
    mmsghdr msgs_[IO_MSG_SIZE];
#endif
    std::atomic<int> msgs_len_;
}; // class KcpListener;


} // namespace net;
} // namespace xq;


#endif // __KCP_LISTENER__
