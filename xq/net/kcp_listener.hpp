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

    uint8_t* data;
    int len;
       KcpSess::Ptr sess;
    
    KcpSeg()
        : data(new uint8_t[KCP_MAX_DATA_SIZE])
        , len(KCP_MAX_DATA_SIZE)
        , sess(nullptr) {
        assert(data);
    }
    
    ~KcpSeg() {
        delete[] data;
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
            wthread_pool_.emplace_back(std::thread(std::bind(&KcpListener::_work_thread, this, q)));
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
        : max_conn_(max_conn)
        , state_(State::Stopped)
        , ufd_(INVALID_SOCKET)
        , host_(host)
        , sessions_(Kcp::sessions()) {
        
        assert(max_conn_ > 0 && "max_conn is invalid");
        
        sessions_.clear();
        for (uint32_t conv = 1; conv <= max_conn; conv++) {
            KcpSess::Ptr s = KcpSess::create(conv);
            s->nodelay(1, 20, 2, 1);
            s->set_output(KcpListener::output);
            sessions_[conv] = s;
        }
    }

    static int output(const char* raw, int len, IKCPCB*, void* user) {
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
        char* rbuf = new char[KCP_MAX_DATA_SIZE];
        int nrecv, rawlen, idx = 0;
        
        const size_t QUE_SIZE = que_vec_.size();

        uint32_t conv;
        KcpSess::Ptr sess;
        KcpSeg *seg;
        int64_t now_ms;

        while (state_ == State::Running) {
            now_ms = xq::tools::now_milli();

            // Step 2: 获取数据
            rawlen = ::recvfrom(ufd_, raw, KCP_MTU, 0, &addr, &addrlen);
            if (rawlen < 0) {
                printf("recvfrom error: %d\n", error());
                continue;
            }

            // Step 3: 获取对应的KcpSession
            conv = Kcp::get_conv(raw);
            sess = sessions_[conv];
            if (sess->check_new(&addr, addrlen)) {
                sess->set_ufd(ufd_);
            }

            // Step 4: 获取KCP消息包
            if (sess->input(raw, rawlen) < 0) {
                continue;
            }

            while (sess->nrcv_que()) {
                // Step 5: 获取消息包
                nrecv = sess->recv(rbuf, KCP_MAX_DATA_SIZE);
                if (nrecv < 0) {
                    break;
                }

                sess->set_last_ms(now_ms);
                seg = KcpSeg::pool()->get();
                seg->sess = sess;
                ::memcpy(seg->data, rbuf, nrecv);
                seg->len = nrecv;

                // Step 6: 投递消息
                assert(que_vec_[idx++]->enqueue(seg));
                if (idx >= QUE_SIZE) {
                    idx = 0;
                }
            }
        }

        delete[] rbuf;
    }

#else // 类UNIX平台 io 工作线程
    void _io() {
        ufd_ = udp_socket(host_.c_str(), nullptr);
        assert(ufd_ != INVALID_SOCKET && "ufd create failed");

        constexpr uint32_t RCVBUF_SIZE = 1024 * 1024 * 32;
        socklen_t opt_len = sizeof(RCVBUF_SIZE);
        assert(::setsockopt(ufd_, SOL_SOCKET, SO_RCVBUF, &RCVBUF_SIZE, opt_len) == 0 && "set udp recv buffer size failed");

        constexpr int MSG_LEN = 128;
        sockaddr addrs[MSG_LEN];
        ::memset(addrs, 0, sizeof(addrs));

        char bufs[MSG_LEN][KCP_MTU];

        mmsghdr msgs[MSG_LEN];
        ::memset(msgs, 0, sizeof(msgs));

        size_t rawlen;
        char *raw;
        char *rbuf = new char[KCP_MAX_DATA_SIZE];
        mmsghdr *msg;

        int i, n, nrecv;
        iovec iovecs[MSG_LEN];
        for (i = 0; i < MSG_LEN; i++) {
            iovecs[i].iov_base = bufs[i];
            iovecs[i].iov_len = KCP_MTU;
            msgs[i].msg_hdr.msg_name = &addrs[i];
            msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
            msgs[i].msg_hdr.msg_iov = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }

        socklen_t addrlen;
        sockaddr *addr;
        uint32_t conv;
        KcpSess::Ptr sess;
        KcpSeg *seg;
        KcpSeg* segs[MSG_LEN];
        size_t seglen;
        int64_t now_ms;

        while(state_ == State::Running) {
            seglen = 0;
            now_ms = xq::tools::now_milli();

            n = ::recvmmsg(ufd_, msgs, MSG_LEN, MSG_WAITFORONE, nullptr);
            if (n < 0) {
                continue;
            }

            for (int i = 0; i < n; i++) {
                msg = &msgs[i];
                raw = bufs[i];
                rawlen = msg->msg_len;
                if (rawlen < KCP_HEAD_SIZE) {
                    continue;
                }

                addrlen = msg->msg_hdr.msg_namelen;
                addr = (sockaddr*)msg->msg_hdr.msg_name;

                conv = Kcp::get_conv(raw);

                sess = sessions_[conv];
                if (sess->check_new(addr, addrlen)) {
                    sess->set_ufd(ufd_);
                }

                if (sess->input(raw, rawlen) < 0) {
                    continue;
                }

                while(sess->nrcv_que()) {
                    nrecv = sess->recv(rbuf, KCP_MAX_DATA_SIZE);
                    if (nrecv < 0) {
                        break;
                    }

                    sess->set_last_ms(now_ms);
                    segs[seglen++] = seg = KcpSeg::pool()->get();
                    seg->sess = sess;
                    ::memcpy(seg->data, rbuf, nrecv);
                    seg->len = nrecv;
                };
            } // for (i = 0; i < n; i++);

            assert(que_.enqueue_bulk(segs, seglen));
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

    void _work_thread(WorkQueue *que) {
        KcpSeg* seg;
        while (state_ == State::Running) {
            que->wait_dequeue(seg);

            KcpSess::Ptr s = seg->sess;
            s->send((char*)seg->data, seg->len);

            KcpSeg::pool()->put(seg);
        }
    }

    const uint32_t max_conn_;

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
