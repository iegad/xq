#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"
#include "third/blockingconcurrentqueue.h"
#include <functional>
#include <unordered_set>

namespace xq {
namespace net {

/// <summary>
/// KcpListener: Kcp 服务端
/// </summary>
class KcpListener {
public:
    typedef std::shared_ptr<KcpListener> Ptr;

    /// <summary>
    /// State: KCP 服务器状态
    /// </summary>
    enum class State {
        Stopped,    // 停止
        Stopping,   // 停止中
        Running     // 运行
    };

    /// <summary>
    /// 创建KcpListener
    /// </summary>
    /// <param name="host">监听地址</param>
    /// <param name="max_conn">最大连接数</param>
    /// <returns></returns>
    static Ptr create(const std::string& host, uint32_t max_conn) {
        return Ptr(new KcpListener(host, max_conn));
    }

    ~KcpListener() {
        for (auto& itr : sessions_) {
            delete itr.second;
        }
    }

    /// <summary>
    /// 启动服务
    /// </summary>
    void run() {
        state_ = State::Running;

        // Step 1: 开启工作线程
        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            RxQueue* q = new RxQueue(2048);
            rques_.push_back(q);
            kp_thread_pool_.emplace_back(std::thread(std::bind(&KcpListener::_kcp_proc, this, q)));
        }

        // Step 2: 开启 kcp update线程
        update_thread_ = std::thread(std::bind(&KcpListener::_update, this));

        // Step 3 :开启 io read 线程
        rx_thread_ = std::thread(std::bind(&KcpListener::_rx, this));

        // Step 4: 等待所有线程关闭
        rx_thread_.join();
        update_thread_.join();

        for (auto &t : kp_thread_pool_) {
            t.join();
        }

        // Step 5: 清空RxSeg队列
        RxSeg* item[IO_BLOCK_SIZE];
        for (auto& que : rques_) {
            while (que->try_dequeue_bulk(item, IO_BLOCK_SIZE));
            delete que;
        }

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
    KcpListener(const std::string& host, uint32_t max_conn)
        : MAX_CONN(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host) {
        assert(MAX_CONN > 0 && "max_conn is invalid");

        for (size_t i = 1; i <= MAX_CONN; i++) {
            KcpSess* sess = new KcpSess(i);
            sess->set_output(&KcpListener::output);
            sess->set_nodelay(1, KCP_UPDATE_MS, 4, 0);
            sessions_.insert(std::make_pair(i, sess));
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
        sess->append_data((uint8_t *)raw, len);
        return 0;
    }

#ifdef WIN32

    /// <summary>
    /// windows 平台 IO 读线程
    /// </summary>
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();

        // Step 1: 创建 udp 监听套接字
        ufd_ = udp_socket(host_.c_str(), nullptr);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");

        int rawlen;
        uint32_t conv;
        RxSeg *seg;
        KcpSess* sess;

        while (state_ == State::Running) {
            // Step 2: 获取数据
            seg = RxSeg::pool()->get();
            seg->addrlen = sizeof(sockaddr);

            rawlen = ::recvfrom(ufd_, (char *)seg->data[0], KCP_MTU, 0, &seg->addr, &seg->addrlen);
            if (rawlen < 0) {
                printf("recvfrom error: %d\n", error());
                continue;
            }

            if (rawlen < KCP_HEAD_SIZE) {
                continue;
            }

            // Step 3: 构建对象
            conv = Kcp::get_conv(seg->data[0]);
            sess = sessions_[conv];
            if (active_convs_.count(conv)) {
                sess->set(ufd_, &seg->addr, seg->addrlen, conns_++ % QUE_SIZE);
                active_convs_.insert(conv);
            }

            // Step 4: 投递消息
            assert(rques_[sess->get_que_idx()]->enqueue(seg));
        }

        close(ufd_);
    }

#else
    /// <summary>
    /// 类Unix 平台 IO 读线程
    /// </summary>
    void _rx() {
        static const size_t QUE_SIZE = std::thread::hardware_concurrency();

        // Step 1: 构建udp socket
        ufd_ = udp_socket(host_.c_str(), nullptr);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        size_t rawlen;
        mmsghdr *msg;
        msghdr *hdr;

        uint32_t conv;
        KcpSess *sess;

        iovec iovecs[IO_MSG_SIZE][IO_BLOCK_SIZE];

        int i, j, n = IO_MSG_SIZE;

        RxSeg *seg;
        RxSeg *segs[IO_MSG_SIZE] = {nullptr};

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
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, nullptr);
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
                if (sess->check(ufd_, &seg->addr, seg->addrlen)) {
                    sess->set_que_idx(conns_++ % QUE_SIZE);
                    active_convs_.insert(conv);
                }
                seg->sess = sess;

                // Step 4: 投递队列
                assert(rques_[sess->get_que_idx()]->enqueue(seg));
            } // for (i = 0; i < n; i++);
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

        int64_t now_ms;
        while (state_ == State::Running) {
            now_ms = xq::tools::now_milli();
            std::this_thread::sleep_for(INTVAL);
            
            std::list<uint32_t> convs = active_convs_.as_list();
            for (auto& conv : convs) {
                if (!sessions_[conv]->update(now_ms)) {
                    active_convs_.erase(conv);
                    conns_--;
                    std::printf("sess[%u] timeout\n", sessions_[conv]->get_conv());
                }
            }
        }
    }

    /// <summary>
    /// kcp 工作线程
    /// </summary>
    /// <param name="que">RxSeg队列</param>
    void _kcp_proc(RxQueue *que) {
        int nrecv;

        uint8_t* raw;
        uint8_t* rbuf = new uint8_t[KCP_MAX_DATA_SIZE];

        RxSeg* seg;
        KcpSess* sess;
        size_t n = 0;

        while (state_ == State::Running) {
            que->wait_dequeue(seg);

            do {
                // Step 1: 获取KcpSess
                sess = seg->sess;

                // Step 2: 获取KCP消息包
                size_t nleft = seg->len, i = 0;

                while (nleft > 0) {
                    n = nleft > KCP_MTU ? KCP_MTU : nleft;
                    raw = seg->data[i++];
                    if (sess->input(raw, n) < 0) {
                        // TODO:
                        break;
                    }
                    nleft -=  n;
                }

                while (true) {
                    // Step 3: 获取消息包
                    nrecv = sess->recv(rbuf, KCP_MAX_DATA_SIZE);
                    if (nrecv < 0) {
                        break;
                    }

                    assert(sess->remote() == addr2str(&seg->addr));
                    if (sess->send(rbuf, nrecv) < 0) {
                        // TODO
                    }
                }
            } while (0);

            RxSeg::pool()->put(seg);
        }

        delete[] rbuf;
    }

    const uint32_t MAX_CONN;                    // 最大连接数
    State state_;                               // 服务端状态

    SOCKET ufd_;                                // 监听套接字
    std::string host_;                          // 监听地址
    std::atomic_int32_t conns_;                 // 当前连接数

    std::vector<std::thread> kp_thread_pool_;   // 工作线程池 kcp procedure thread pool
    std::thread rx_thread_;                     // io read thread
    std::thread update_thread_;                 // kcp update thread

    std::vector<RxQueue*> rques_;               // read data queue

    xq::tools::Set<uint32_t> active_convs_;
    std::unordered_map<uint32_t, KcpSess*> sessions_; // 会话
}; // class KcpListener;

} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__
