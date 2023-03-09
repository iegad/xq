// ==================================================================================================================================================
// KcpListener 定义
//
// @author: iegad
// @time:   2023-02-15
// @note:
//      类: 
//          1, KcpListener:       Kcp服务端
//          2, KcpListener::Sess: 会话
//          3, KcpListener::Seg:  数据段
//
//      实现:
//          KcpListener服务端启动时, 会运行三类工作线程:
//            * _rx:       IO read 线程
//            * _update:   kcp udpate 线程
//            * _kcp_proc: kcp 数据处理线程.
//          其中 _rx, _update各占用一条物理线程, _kcp_proc线程数为 CPU核心数.
//
// ==================================================================================================================================================
// 修改记录:
// --------------------------------------------------------------------------------------------------------------------------------------------------
//   @update_time    | @coder        | @comment
// --------------------------------------------------------------------------------------------------------------------------------------------------
//
// ==================================================================================================================================================


#ifndef __XQ_NET_KCP_LISTENER__
#define __XQ_NET_KCP_LISTENER__


#include <functional>
#include <unordered_set>


#include "xq/net/net.hpp"
#include "xq/net/kcp.hpp"
#include "xq/third/blockingconcurrentqueue.h"
#include "xq/tools/tools.hpp"


namespace xq {
namespace net {


typedef std::mutex LockType;


template <class TEvent>
class KcpListener final {
public:
    typedef std::shared_ptr<KcpListener> Ptr;


// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  BEG Sess +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
class Sess final {
public:
    friend class KcpListener<TEvent>;


    // ========================
    // Sess 对象池
    // ========================
    static xq::tools::ObjectPool<Sess>* pool() {
        return xq::tools::ObjectPool<Sess>::instance();
    }


    // ========================
    // 构造函数
    // ========================
    explicit Sess()
        : kcp_(nullptr)
        , que_num_(~0)
        , time_ms_(0)
        , last_ms_(0)
        , raddr_({0, {0}})
        , raddrlen_(sizeof(raddr_))
        , listener_(nullptr) {}


    // ========================
    // 析构函数
    // ========================
    ~Sess() {
        if (kcp_) {
            delete kcp_;
        }
    }


    // ========================
    // 获取对端地址
    // ========================
    const std::string& remote() {
        if (remote_.empty()) {
            remote_ = xq::net::addr2str(&raddr_);
        }
        return remote_;
    }


    int64_t last_ms() const {
        return last_ms_;
    }


    int64_t time_ms() const {
        return time_ms_;
    }


    // ========================
    // 获取kcp conv 
    // ========================
    uint32_t conv() const {
        return kcp_->conv();
    }


    // ========================
    // 发送数据
    //   这里发送数据, 指的是将数据写入Kcp发送队列.
    //
    // @buf:
    // @len:
    //
    // @return: 成功返回0, 否则返回非0.
    // ========================
    int send(const uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        int n = kcp_->send(buf, len);
        if (n == 0) {
            kcp_->flush();
        }
        return n;
    }


private:


    bool _diff_addr(const sockaddr* addr, socklen_t addrlen) {
        return addrlen != raddrlen_ || ::memcmp(addr, &raddr_, addrlen);
    }


    // ------------------------
    // 设置Sess参数
    //
    // @conv: Kcp conv
    // @listener: 会话所属服务端
    // @addr:     会话地址
    // @addrlen:
    // @now_ms:   当前时间(毫秒)
    // @que_num:  工作队列number
    // @output:   KcpListener::output
    // ------------------------
    void _set(uint32_t conv, KcpListener<TEvent>* listener, sockaddr* addr, socklen_t addrlen, int64_t now_ms, uint32_t que_num, int (*output)(const uint8_t* buf, size_t len, void* user)) {
        if (!kcp_) {
            kcp_ = new Kcp(conv, this, output);
        }
        else {
            kcp_->reset(conv);
        }

        if (!listener_) {
            listener_ = listener;
        }

        ::memcpy(&raddr_, addr, addrlen);
        raddrlen_ = addrlen;
        remote_.clear();
        que_num_ = que_num;
        last_ms_ = time_ms_ = now_ms;
    }


    void _reset(sockaddr* addr, socklen_t addrlen, int64_t now_ms) {
        kcp_->reset(kcp_->conv());
        ::memcpy(&raddr_, addr, addrlen);
        raddrlen_ = addrlen;
        remote_.clear();
        last_ms_ = time_ms_ = now_ms;
    }


    // ------------------------
    // Kcp input
    //
    // @raw:  原始数据
    // @size: 原始数据长度
    // 
    // @return: 成功返回0, 否则返回非0.
    // ------------------------
    int _input(const uint8_t* raw, size_t size) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->input(raw, size);
    }


    // ------------------------
    // 获取Kcp消息
    // 
    // @buf: 接收缓冲区
    // @len: 缓冲区长度
    //
    // @return: 成功, 返回消息长度.
    //          -1, 接收队列为空, 表示已没有可接收的数据.
    //          其余非0值为错误.
    // ------------------------
    int _recv(uint8_t* buf, int len) {
        std::lock_guard<LockType> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }


    // ------------------------
    // Kcp udpate
    //
    // @now_ms: 刷新时间(毫秒), 并非当前时间. 是Kcp的相应时间: now_ms - connection_time .
    //
    // @return: 成功返回 0, 连接超时返回 -1.
    // ------------------------
    int _update(int64_t now_ms) {
        int64_t last_ms = last_ms_;

        if (last_ms == 0 || now_ms - last_ms > KCP_TIMEOUT) {
            return -1;
        }

        std::lock_guard<LockType> lk(kcp_mtx_);
        kcp_->update((uint32_t)(now_ms - time_ms_));

        return 0;
    }


    Kcp*                 kcp_;       // KCP实例
    uint32_t             que_num_;   // 当前工作队列号
    int64_t              time_ms_;   // 激活时间
    std::atomic<int64_t> last_ms_; // 会话有效时间
    sockaddr             raddr_;     // 对端地址
    socklen_t            raddrlen_;
    KcpListener<TEvent>* listener_;
    std::string          remote_;    // 对端址地表达式
    LockType             kcp_mtx_;   // kcp锁


    Sess(const Sess&) = delete;
    Sess& operator=(const Sess&) = delete;
}; // class Sess;

// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  END Sess +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ BEG Seg +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
struct Seg {
    // Seg 对象池
    static xq::tools::ObjectPool<Seg>* pool() {
        return xq::tools::ObjectPool<Seg>::instance();
    }


    int       len;                // 段长
    Sess*     sess;               // 段来源
    socklen_t addrlen;            // 地址长度
    sockaddr  addr;               // 地址
    int64_t   time_ms;            // 段时间
    uint8_t   data[IO_RBUF_SIZE]; // 数据


    Seg() {
        ::memset(this, 0, sizeof(*this));
        assert(data);
    }
}; // struct RxSeg;
// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ END Seg +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


private:
    // ------------------------
    // Seg 工作队列
    // ------------------------
    typedef moodycamel::BlockingConcurrentQueue<Seg*> Queue;


public:
    // ========================
    // 创建KcpListener
    //
    // @host:     监听地址
    // @max_conn: 最大连接数
    // ========================
    static Ptr create(const std::string& host, uint32_t max_conn) {
        assert(host.size() > 0 && "host is invalid");
        assert(max_conn > 0 && "max_conn is invalid");
        return Ptr(new KcpListener(host, max_conn));
    }


    // ========================
    // 析构函数
    // ========================
    ~KcpListener() {
//#ifndef WIN32
//        for (auto &msg: msgs_) {
//            iovec *iov = msg.msg_hdr.msg_iov;
//            delete[] (uint8_t*)iov[0].iov_base;
//            delete[] iov;
//        }
//#endif // WIN32

        for (auto &q : rques_) {
            Seg* segs[128];
            int n;
            while (n = q->try_dequeue_bulk(segs, 128), n > 0) {
                Seg::pool()->put(segs, n);
            }
            delete q;
        }

        if (event_) {
            delete event_;
        }
    }


    // ========================
    // 最大kcp conv
    // ========================
    uint32_t max_conv() const {
        return MAX_CONV;
    }


    // ========================
    // 当前连接数
    // ========================
    int32_t conns() const {
        return conns_;
    }


    const std::string& host() const {
        return host_;
    }


    // ========================
    // 启动服务
    // ========================
    void run() {
#if (KL_EVENT_ON_START == 1)
        if (event_->on_start(this) < 0) {
            return;
        }
#endif // KL_EVENT_ON_START;

        // Step 1: 创建 udp 监听套接字
        ufd_ = udp_bind(host_);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");
#ifdef WIN32
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&IO_TIMEOUT, sizeof(IO_TIMEOUT)));
#else // LINUX平台下
        constexpr timeval TIMEOUT = {.tv_sec = IO_TIMEOUT / 1000, .tv_usec = 0};
        assert(!::setsockopt(ufd_, SOL_SOCKET, SO_RCVTIMEO, &TIMEOUT, sizeof(TIMEOUT)));
#endif // WIN32

        state_ = State::Running;

        // Step 2: 开启工作线程
        for (Queue *q : rques_) {
            kp_thread_pool_.emplace_back(std::bind(&KcpListener::_kcp_proc, this, q));
        }

        // Step 3: 开启 kcp update线程
        update_thread_ = std::thread(std::bind(&KcpListener::_update, this));

        // Step 4 :开启 主线程 io read
        _rx();

        // Step 5: 等待update线程
        update_thread_.join();

        // Step 6: 等待工作线程
        for (auto &t : kp_thread_pool_) {
            t.join();
        }

        // Step 7: 清理工作线程
        kp_thread_pool_.clear();

        // Step 8: 清空RxSeg队列
        Seg* segs[128];
        size_t n = 0;
        for (auto& que : rques_) {
            while (n = que->try_dequeue_bulk(segs, 128), n > 0) {
                Seg::pool()->put(segs, n);
            }
        }

        // Step 9: 关闭UDP监听
        ufd_ = INVALID_SOCKET;

        Sess** ss = new Sess*[MAX_CONV];
        n = sessions_.get_all_vals(ss, MAX_CONV);
        if (n > 0) {
            Sess::pool()->put(ss, n);
        }
        sessions_.clear();
        delete[] ss;

        conns_ = sessions_.size();
        state_ = State::Stopped;
#if (KL_EVENT_ON_STOP == 1)
        event_->on_stop(this);
#endif // KL_EVENT_ON_STOP;
    }


    // ========================
    // 停止服务
    // ========================
    void stop() {
        if (state_ == State::Running && ufd_ != INVALID_SOCKET) {
            close(ufd_);
            state_ = State::Stopping;
        }
    }


private:


    // ------------------------
    // 构造函数
    // ------------------------
    KcpListener(const std::string& host, uint32_t max_conn)
        : MAX_CONV(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , event_(new TEvent)
        /*, msgs_len_(0)*/ {
//#ifndef WIN32
//        for (size_t i = 0; i < IO_MSG_SIZE; i++) {
//            msgs_[i].msg_hdr.msg_control = nullptr;
//            msgs_[i].msg_hdr.msg_controllen = 0;
//            msgs_[i].msg_hdr.msg_flags = 0;
//            msgs_[i].msg_hdr.msg_name = nullptr;
//            msgs_[i].msg_hdr.msg_namelen = 0;
//            iovec *iov = new iovec[1];
//            iov[0].iov_base = new uint8_t[IO_RBUF_SIZE];
//            iov[0].iov_len = IO_RBUF_SIZE;
//            msgs_[i].msg_hdr.msg_iov = iov;
//            msgs_[i].msg_hdr.msg_iovlen = 1;
//            msgs_[i].msg_len = 0;
//        }
//#endif // WIN32

        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            rques_.emplace_back(new Queue(2048));
        }
    }


    // ------------------------
    // 发送数据, 该方法只在Linux平台下有效
    // ------------------------
//    void _sendmsgs() {
//#ifndef WIN32
//        int len = msgs_len_;
//        if (len > 0) {
//            int n = ::sendmmsg(ufd_, msgs_, len, 0);
//            if (n < 0) {
//                event_->on_error(xq::net::ErrType::IO_SEND, error(), nullptr);
//            }
//            msgs_len_ = 0;
//        }
//#endif // !WIN32
//    }

    // ------------------------
    // kcp output
    // ------------------------
    static int output(const uint8_t* raw, size_t len, void* user) {
        assert(len > 0 && len <= KCP_MTU);

        Sess* sess = (Sess*)user;
        KcpListener* l = sess->listener_;

#if (KL_EVENT_ON_SEND == 1)
        l->event_->on_send((const uint8_t*)raw, len, &sess->raddr_, sess->raddrlen_);
#endif // KL_EVENT_ON_SEND

        if (::sendto(l->ufd_, (const char*)raw, len, 0, &sess->raddr_, sess->raddrlen_) < 0) {
            sess->listener_->event_->on_error(xq::net::ErrType::IO_SEND, error(), sess);
        }

        return 0;
    }


#ifdef WIN32

    // ------------------------
    // IO 接收线程
    // ------------------------
    void _rx() {
        const size_t QUE_SIZE  = std::thread::hardware_concurrency();

        using SegPool = xq::tools::ObjectPool<Seg>;
        using KcpPool = xq::tools::ObjectPool<Sess>;

        int      rawlen, err, nremote, qidx = 0;
        uint32_t conv;
        int64_t  now_ms;

        Seg*     seg = nullptr;
        Sess*    sess;
        SegPool* segpool = Seg::pool();
        KcpPool* kcppool = Sess::pool();

        while (state_ == State::Running) {
            // Step 1: 申请seg
            if (!seg) {
                seg = segpool->get();
                assert(seg);
            }
            seg->addrlen = sizeof(sockaddr);

            // Step 2: 接收数据
            rawlen = ::recvfrom(ufd_, (char *)seg->data, IO_RBUF_SIZE, 0, &seg->addr, &seg->addrlen);

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

                // Step 3: 获取 kcp conv
                conv = Kcp::get_conv(seg->data);
                if (conv == 0 || conv > MAX_CONV) {
                    event_->on_error(xq::net::ErrType::KL_INVALID_CONV, conv, &seg->addr);
                    break;
                }

#if (KL_EVENT_ON_RECV == 1)
                if (event_->on_recv(seg) < 0) {
                    break;
                }
#endif // KL_EVENT_ON_RECV

                now_ms = xq::tools::now_milli();
                // Step 4: 获取会话
                if (sessions_.get(conv, sess)) {
                    if (sess->_diff_addr(&seg->addr, seg->addrlen)) {
                        sess->_reset(&seg->addr, seg->addrlen, now_ms);

#if (KL_EVENT_ON_RECONNECTED == 1)
                        if (event_->on_reconnected(sess) < 0) {
                            break;
                        }
#endif // KL_EVENT_ON_RECONNECTED;

                    }
                } else {
                    sess = kcppool->get();
                    sess->_set(conv, this, &seg->addr, seg->addrlen, now_ms, qidx++ % QUE_SIZE, &KcpListener::output);

#if (KL_EVENT_ON_CONNECTED == 1)
                    if (event_->on_connected(sess) < 0) {
                        kcppool->put(sess);
                        break;
                    }
#endif // KL_EVENT_ON_CONNECTED;

                    conns_++;
                    assert(sessions_.insert(conv, sess).second);
                }

                // Step 5: 构建 seg
                seg->sess    = sess;
                seg->time_ms = now_ms;
                seg->len     = rawlen;

                // Step 6: 投递消息
                assert(rques_[sess->que_num_]->enqueue(seg));
                seg = nullptr;
            } while (0);
        }
    }


#else
    // ------------------------
    // Linux KCP output
    //    将数据添加到mmsghdr缓冲区, 当mmsghdr缓冲区满时调用底层方法
    // ------------------------
//    static int output(const uint8_t* raw, size_t len, void* user) {
//        assert(len > 0 && (size_t)len <= KCP_MTU);
//
//        Sess*        sess = (Sess*)user;
//        KcpListener* l    = sess->listener_;
//        if (::sendto(l->ufd_, raw, len, 0, &sess->raddr_, sess->raddrlen_) < 0) {
//            std::printf("sendto failed: %d\n", error());
//        }
//
//        if (l->msgs_len_ == IO_MSG_SIZE / 2) {
//            l->_sendmsgs();
//        }
//
//        size_t       i    = l->msgs_len_++;
//        msghdr*      msg  = &l->msgs_[i].msg_hdr;
//
//        msg->msg_name           = &sess->raddr_;
//        msg->msg_namelen        = sess->raddrlen_;
//        msg->msg_iov[0].iov_len = len;
//
//#if (KL_EVENT_ON_SEND == 1)
//        l->event_->on_send((const uint8_t*)raw, len, &sess->raddr_, sess->raddrlen_);
//#endif // KL_EVENT_ON_SEND;
//        ::memcpy(msg->msg_iov[0].iov_base, raw, len);
//
//        if (l->msgs_len_ == IO_MSG_SIZE / 2) {
//            l->_sendmsgs();
//        }
//
//        return 0;
//    }


    // ------------------------
    // Linux IO 工作线程
    // ------------------------
    void _rx() {
        const  size_t   QUE_SIZE  = std::thread::hardware_concurrency();

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        uint32_t conv;
        
        int      i,  n = IO_MSG_SIZE, qidx = 0, err;
        size_t   rawlen;
        int64_t  now_ms;

        mmsghdr* msg;
        msghdr*  hdr;
        Sess*    sess;
        
        Seg*     seg;
        Seg*     segs[IO_MSG_SIZE] = {nullptr};
        iovec    iovecs[IO_MSG_SIZE];

        while (state_ == State::Running) {

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

            sess = nullptr;
            // Step 2: 获取消息
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, nullptr);

            do {
                if (n < 0) {
                    err = error();
                    if (err != EAGAIN && err != EINTR) {
                        event_->on_error(xq::net::ErrType::IO_RECV, err, nullptr);
                    }
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
                    if (conv == 0 || conv > MAX_CONV) {
                        event_->on_error(xq::net::ErrType::KL_INVALID_CONV, conv, &seg->addr);
                        continue;
                    }

                    seg->addrlen = msg->msg_hdr.msg_namelen;

#if (KL_EVENT_ON_RECV == 1)
                    if (event_->on_recv(seg->data, rawlen, &seg->addr, seg->addrlen) < 0) {
                        continue;
                    }
#endif // KL_EVENT_ON_RECV;

                    // Step 4: 获取会话
                    if (sessions_.get(conv, sess)) {
                        if (sess->_diff_addr(&seg->addr, seg->addrlen)) {
                            sess->_reset(&seg->addr, seg->addrlen, now_ms);
#if (KL_EVENT_ON_RECONNECTED == 1)
                            if (event_->on_reconnected(sess) < 0) {
                                continue;
                            }
#endif // KL_EVENT_ON_RECONNECTED;
                        }
                    } else {
                        sess = Sess::pool()->get();
                        sess->_set(conv, this, &seg->addr, seg->addrlen, now_ms, qidx++ % QUE_SIZE, &KcpListener::output);

#if (KL_EVENT_ON_CONNECTED == 1)
                        if (event_->on_connected(sess) < 0) {
                            Sess::pool()->put(sess);
                            continue;
                        }
#endif // KL_EVENT_ON_CONNECTED;

                        conns_++;
                        assert(sessions_.insert(conv, sess).second);
                    }

                    // Step 5: 构建Seg
                    seg->sess    = sess;
                    seg->time_ms = now_ms;
                    seg->len     = rawlen;

                    // Step 6: 投递队列
                    assert(rques_[sess->que_num_]->enqueue(seg));
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


    // ------------------------
    // KCP UPDATE 线程
    // ------------------------
    void _update() {
        typedef Sess* SessPtr;

        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(1);

        uint32_t  i, n, nerase;
        int64_t   now_ms;
        SessPtr   sess;
        uint32_t* erase_keys  = new uint32_t[MAX_CONV];
        SessPtr*  erase_sess  = new SessPtr[MAX_CONV];
        SessPtr*  active_sess = new SessPtr[MAX_CONV];

        while (state_ == State::Running) {
            nerase = 0;
            std::this_thread::sleep_for(INTVAL);
            
            now_ms = xq::tools::now_milli();
            n      = sessions_.get_all_vals(active_sess, MAX_CONV);
            assert(n < MAX_CONV);

            for (i = 0; i < n; i++) {
                sess = active_sess[i];
                if (sess->_update(now_ms) < 0) {
                    erase_sess[nerase]   = sess;
                    erase_keys[nerase++] = sess->conv();
#if (KL_EVENT_ON_DISCONNECTED == 1)
                    event_->on_disconnected(sess);
#endif // KL_EVENT_ON_DISCONNECTED;
                }
            }

            // _sendmsgs();

            sessions_.erase(erase_keys, nerase);
            Sess::pool()->put(erase_sess, nerase);
        }

        delete[] erase_keys;
        delete[] erase_sess;
        delete[] active_sess;
    }


    // ------------------------
    // KCP 工作线程
    // ------------------------
    void _kcp_proc(Queue *que) {
        constexpr int64_t TIMEOUT = std::chrono::microseconds(IO_TIMEOUT * 1000).count();

        int      n;
        size_t   i, nseg;
        Seg      *seg, *segs[16];
        Sess*    sess;
        auto     segpool = Seg::pool();
        uint8_t* rbuf    = new uint8_t[KCP_MAX_DATA_SIZE];

        while (state_ == State::Running) {
            nseg = que->wait_dequeue_bulk_timed(segs, 16, TIMEOUT);
            for (i = 0; i < nseg; i++) {
                // Step 1: 获取Sess
                seg  = segs[i];
                sess = seg->sess;
                assert(!sess->_diff_addr(&seg->addr, seg->addrlen));

                // Step 2: 获取KCP消息包
                n = sess->_input(seg->data, seg->len);
                if (n < 0) {
                    event_->on_error(xq::net::ErrType::KCP_INPUT, n, sess);
                    sess->last_ms_ = 0;
                    continue;
                }

                // Step 3: 获取消息包
                while (n = sess->_recv(rbuf, KCP_MAX_DATA_SIZE), n > 0) {
                    sess->last_ms_ = event_->on_message(sess, rbuf, n) < 0 ? 0 : seg->time_ms;
                }
                std::this_thread::yield();
            }

            if (nseg > 0) {
                segpool->put(segs, nseg);
            }
        } // while (state_ == State::Running);

        delete[] rbuf;
    }


    const uint32_t                  MAX_CONV;        // 最大连接数
    State                           state_;          // 服务端状态
    SOCKET                          ufd_;            // 监听套接字
    std::string                     host_;           // 监听地址
    std::atomic<int32_t>            conns_;          // 当前连接数
    std::thread                     update_thread_;  // kcp update thread
    std::vector<std::thread>        kp_thread_pool_; // 工作线程池 kcp procedure thread pool
    std::vector<Queue*>             rques_;          // read data queue
    xq::tools::Map<uint32_t, Sess*> sessions_;       // 会话
    TEvent*                         event_;          // 事件实例

//#ifndef WIN32
//    mmsghdr msgs_[IO_MSG_SIZE];
//#endif
//    std::atomic<int> msgs_len_;
}; // class KcpListener;


} // namespace net;
} // namespace xq;


#endif // __XQ_NET_KCP_LISTENER__
