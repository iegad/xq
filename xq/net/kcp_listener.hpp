#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"
#include "third/blockingconcurrentqueue.h"
#include <functional>
#include <unordered_set>

namespace xq {
namespace net {

struct KcpSeg {
    static xq::tools::ObjectPool<KcpSeg>* pool() {
        return xq::tools::ObjectPool<KcpSeg>::Instance();
    }

    int len;
    sockaddr addr;
    socklen_t addrlen;
    uint8_t raw[KCP_MTU * 8];

    explicit KcpSeg()
        : len(KCP_MAX_DATA_SIZE)
        , addr({ 0,{0} })
        , addrlen(sizeof(addr)) {
        assert(raw);
    }
}; // struct KcpSeg;

class KcpListener {
public:
    typedef std::shared_ptr<KcpListener> Ptr;
    typedef moodycamel::BlockingConcurrentQueue<KcpSeg*> WorkQueue;

    enum class State {
        Stopped,
        Stopping,
        Running
    };

    static Ptr create(const std::string& host, uint32_t max_conn) {
        return Ptr(new KcpListener(host, max_conn));
    }

    ~KcpListener() {
        close(ufd_);
    }

    void run() {
        state_ = State::Running;

        for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
            WorkQueue* q = new WorkQueue(1024);
            que_vec_.push_back(q);
            wthread_pool_.emplace_back(std::thread(std::bind(&KcpListener::_kcp_proc, this, q)));
        }

        update_thread_ = std::thread(std::bind(&KcpListener::_update, this));
        io_thread_ = std::thread(std::bind(&KcpListener::_io, this));

        io_thread_.join();
        update_thread_.join();

        for (auto &t : wthread_pool_) {
            t.join();
        }

        KcpSeg* item[10];
        for (auto& que : que_vec_) {
            while (que->try_dequeue_bulk(item, 10));
            delete que;
        }

        state_ = State::Stopped;
    }

    void stop() {
        state_ = State::Stopping;
    }

private:
    KcpListener(const std::string& host, uint32_t max_conn)
        : MAX_CONN(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , sessions_(Kcp::sessions()) {

        assert(MAX_CONN > 0 && "max_conn is invalid");

        sessions_.clear();
        for (uint32_t conv = 1; conv <= max_conn; conv++) {
            KcpSess::Ptr s = KcpSess::create(conv);
            s->nodelay(1, 20, 2, 1);
            s->set_output(KcpListener::output);
            sessions_[conv] = s;
        }
    }

    static int output(const char* raw, int len, IKCPCB* , void* user) {
        KcpSess* s = (KcpSess*)user;
        std::pair<sockaddr*, socklen_t> addr = s->addr();

        if (::sendto(s->ufd(), raw, len, 0, addr.first, addr.second) < 0) {
            printf("sendto error: %d\n", error());
        }

        return 0;
    }

#ifdef WIN32 // WINDOWS平台 io 工作线程

    void _io() {
        // Step 1: 创建 udp 监听套接字
        ufd_ = udp_socket(host_.c_str(), nullptr);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");

        sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        char raw[KCP_MTU];
        int rawlen, idx = 0;

        const size_t QUE_SIZE = que_vec_.size();
        KcpSeg *seg;

        while (state_ == State::Running) {
            // Step 1: 获取数据
            rawlen = ::recvfrom(ufd_, raw, KCP_MTU, 0, &addr, &addrlen);
            if (rawlen < 0) {
                printf("recvfrom error: %d\n", error());
                continue;
            }

            if (rawlen < KCP_HEAD_SIZE) {
                continue;
            }

            // Step 2: 构建对象
            seg = KcpSeg::pool()->get();
            seg->len = rawlen;
            seg->addrlen = addrlen;
            ::memcpy(seg->raw, raw, rawlen);
            ::memcpy(&seg->addr, &addr, addrlen);

            // Step 3: 投递消息
            assert(que_vec_[idx++]->enqueue(seg));
            if (idx >= QUE_SIZE) {
                idx = 0;
            }
        }
    }

#else // 类UNIX平台 io 工作线程
    void _io() {
        const size_t QUE_SIZE = que_vec_.size();

        ufd_ = udp_socket(host_.c_str(), nullptr);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");


        socklen_t opt_len = sizeof(IO_RCVBUF_SIZE);
        assert(::setsockopt(ufd_, SOL_SOCKET, SO_RCVBUF, &IO_RCVBUF_SIZE, opt_len) == 0 && "set udp recv buffer size failed");

        uint8_t bufs[IO_MSG_SIZE][IO_BLOCK_SIZE][KCP_MTU], *raw, *p;

        sockaddr addrs[IO_MSG_SIZE];
        ::memset(addrs, 0, sizeof(addrs));

        mmsghdr msgs[IO_MSG_SIZE];
        ::memset(msgs, 0, sizeof(msgs));

        size_t rawlen = 0, idx = 0, nleft, ncpy;
        mmsghdr *msg;

        iovec iovecs[IO_MSG_SIZE][IO_BLOCK_SIZE];

        int i, j, n;
        for (i = 0; i < IO_MSG_SIZE; i++) {
            msgs[i].msg_hdr.msg_name = &addrs[i];
            msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
            msgs[i].msg_hdr.msg_iov = iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = IO_BLOCK_SIZE;

            for (j = 0; j < 4; j++) {
                iovecs[i][j].iov_base = bufs[i][j];
                iovecs[i][j].iov_len = KCP_MTU;
            }
        }

        socklen_t addrlen;
        KcpSeg *seg;

        while(state_ == State::Running) {
            n = ::recvmmsg(ufd_, msgs, IO_MSG_SIZE, MSG_WAITFORONE, nullptr);
            for (i = 0; i < n; i++) {
                msg = &msgs[i];
                rawlen = msg->msg_len;
                if (rawlen < KCP_HEAD_SIZE) {
                    continue;
                }

                seg = KcpSeg::pool()->get();
                seg->addrlen = addrlen  = msg->msg_hdr.msg_namelen;
                seg->len = nleft = rawlen;
                ::memcpy(&seg->addr, (sockaddr*)msg->msg_hdr.msg_name, addrlen);

                for (j = 0; j < 4 && nleft > 0; j++) {
                    raw = bufs[i][j];
                    p = seg->raw + (rawlen - nleft);
                    ncpy = nleft > KCP_MTU ? KCP_MTU : nleft;
                    ::memcpy(p, raw, ncpy);
                    nleft -= ncpy;
                }

                 assert(que_vec_[idx++]->enqueue(seg));
                 if (idx >= QUE_SIZE) {
                    idx = 0;
                 }
            } // for (i = 0; i < n; i++);
        }
    }
#endif // WIN32

    void _update() {
        constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(10);

        int64_t now_ms;

        while (state_ == State::Running) {
            now_ms = xq::tools::now_milli();
            std::this_thread::sleep_for(INTVAL);
            for (auto &itr: sessions_) {
                itr.second->update(now_ms);
            }
        }
    }

    void _kcp_proc(WorkQueue *que) {
        int nrecv;
        uint32_t conv;

        uint8_t* raw;
        uint8_t* rbuf = new uint8_t[KCP_MAX_DATA_SIZE];

        KcpSeg* seg;
        KcpSess::Ptr sess;

        while (state_ == State::Running) {
            que->wait_dequeue(seg);
            raw = seg->raw;
            assert(raw);

            do {
                // Step 1: 获取对应的KcpSession
                conv = Kcp::get_conv(raw);
                if (conv == 0 || conv > MAX_CONN) {
                    break;
                }

                sess = sessions_[conv];
                assert(sess);
                if (sess->check_new(&seg->addr, seg->addrlen)) {
                    sess->set_ufd(ufd_);
                }

                // Step 2: 获取KCP消息包
                if (sess->input(raw, seg->len) < 0) {
                    continue;
                }

                while (sess->nrcv_que()) {
                    // Step 3: 获取消息包
                    nrecv = sess->recv(rbuf, KCP_MAX_DATA_SIZE);
                    if (nrecv < 0) {
                        break;
                    }

                    if (sess->send(rbuf, nrecv) < 0) {
                        // TODO
                    }
                }
            } while (0);

            KcpSeg::pool()->put(seg);
        }

        delete[] rbuf;
    }

    const uint32_t MAX_CONN;

    State state_;

    SOCKET ufd_;
    std::string host_;

    std::vector<std::thread> wthread_pool_;
    std::thread io_thread_;
    std::thread update_thread_;

    std::vector<WorkQueue*> que_vec_;

    std::unordered_map<uint32_t, KcpSess::Ptr> &sessions_;
}; // class KcpListener;

} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__
