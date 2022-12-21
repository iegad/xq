#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"

namespace xq {
namespace net {

template <typename TEvent>
class KcpListener {
public:
    typedef std::unique_ptr<KcpListener> ptr;
    typedef xq::tools::Map<uint32_t, KcpSess::Ptr> SessMap;

    enum class State {
        Stopped = 0,
        Stopping,
        Runing,
    };

    static ptr create(const std::string &host, uint32_t timeout = 0, uint32_t nthread = 0) {
        if (!timeout)
            timeout = KCP_DEFAULT_TIMEOUT;

        if (!nthread)
            nthread = std::thread::hardware_concurrency();

        return ptr(new KcpListener(host, timeout, nthread));
    }

    ~KcpListener() {
        stop();
    }

    void run() {
        update_thr_ = std::thread(std::bind(&KcpListener::_update_thr, this));

        state_ = State::Runing;

        for (uint32_t i = 0; i < nthread_; i++) {
            SOCKET sockfd = udp_socket(host_.c_str(), nullptr);
            assert(sockfd != INVALID_SOCKET);
            ufds_.emplace_back(sockfd);

            recv_pool_.emplace_back(std::thread(std::bind(&KcpListener::_recv_thr, this, sockfd)));
        }

        update_thr_.join();

        for (uint32_t i = 0; i < nthread_; i++) {
            std::thread& rt = recv_pool_[i];
            rt.join();
        }

        for (auto ufd : ufds_)
            close(ufd);

        recv_pool_.clear();
        state_ = State::Stopped;
    }

    void stop() {
        state_ = State::Stopping;
        for (auto ufd : ufds_)
            close(ufd);

        ufds_.clear();
    }

private:
    explicit KcpListener(const std::string &host, uint32_t timeout, uint32_t nthread)
        : host_(host)
        , state_(State::Stopped)
        , timeout_(timeout)
        , nthread_(nthread) {
        assert(timeout_ > 0 && "timeout is invalid");
        assert(nthread_ > 0 && "nthread is invalid");
    }

    std::pair<SessMap::iterator, bool> add_sess(uint32_t conv, KcpSess::Ptr s) {
        return sess_map_.insert(conv, s);
    }

    SessMap::iterator remove_sess(SessMap::iterator itr) {
        return sess_map_.erase(itr);
    }

    static int _udp_output(const char* data, int datalen, IKCPCB*, void* user) {
        KcpSess* s = (KcpSess*)user;
        auto addr = s->addr();
        if (::sendto(s->sockfd(), data, datalen, 0, addr.first, addr.second) < 0)
            printf("send failed\n");

        return 0;
    }

    void _update_thr() {
        int64_t now_ms, timeout = timeout_;

        while (State::Runing == state_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(xq::net::KCP_UPDATE_MS));
            now_ms = xq::tools::now_milli();
            for (auto itr = sess_map_.begin(); itr != sess_map_.end(); ++itr) {
                auto& s = itr->second;

                if (s->update(now_ms, timeout) < 0) {
                    remove_sess(itr++);
                }
            }
        }
    }
#ifdef _WIN32
    void _recv_thr(SOCKET sockfd) {
        int n;
        uint8_t rbuf[KCP_MTU];
        uint8_t* data = new uint8_t[KCP_MAX_DATA_SIZE];
        size_t datalen;

        sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        ::memset(&addr, 0, sizeof(addr));

        uint32_t conv;
        KcpSess* sess;
        KcpSess::Ptr nconn;
        xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator itr;
        std::pair<xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator, bool> pair;

        while (State::Runing == state_) {
            n = ::recvfrom(sockfd, (char*)rbuf, KCP_MTU, 0, &addr, &addrlen);
            if (n <= 0) {
                event_.on_error(ErrType::ET_ListenerRead, this, error());
                continue;
            }

            event_.on_recv(sockfd, &addr, addrlen, rbuf, n);

            if (n < KCP_HEAD_SIZE) {
                event_.on_error(ErrType::ET_SessRead, &addr, addrlen);
                continue;
            }

            conv = *(uint32_t*)rbuf;
            itr = sess_map_.find(conv);
            if (itr == sess_map_.end()) {
                nconn = KcpSess::create(conv, &KcpListener::_udp_output);
                if (event_.on_connected(nconn.get()) < 0)
                    continue;

                pair = add_sess(conv, nconn);
                assert(pair.second);
                itr = pair.first;
            }
            sess = itr->second.get();
            if (sess->change(sockfd, &addr, addrlen)) {
                // TODO:
            }

            if (sess->input(rbuf, n) < 0) {
                event_.on_error(ErrType::ET_SessRead, &addr, addrlen);
                remove_sess(itr);
                continue;
            }

            datalen = KCP_MAX_DATA_SIZE;
            n = sess->recv(data, datalen);
            if (n <= 0)
                continue;

            sess->flush();

            if (event_.on_message(sess, data, n) < 0)
                remove_sess(itr);
        }

        delete []data;
    }
#else
    void _recv_thr(SOCKET sockfd) {
        static constexpr int MSG_LEN = 64;
        int i, n;

        sockaddr addrs[MSG_LEN];
        ::memset(&addrs, 0, sizeof(addrs));

        uint8_t bufs[MSG_LEN][KCP_MTU];

        mmsghdr msgs[MSG_LEN];
        ::memset(msgs, 0, sizeof(msgs));

        iovec iovecs[MSG_LEN];
        for (i = 0; i < MSG_LEN; i++) {
            iovecs[i].iov_base = bufs[i];
            iovecs[i].iov_len = KCP_MTU;
            msgs[i].msg_hdr.msg_name = &addrs[i];
            msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
            msgs[i].msg_hdr.msg_iov = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
        }

        uint32_t conv;
        KcpSess::Ptr s;
        KcpSess* sess;
        xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator itr;
        std::pair<xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator, bool> pair;
        std::vector<uint8_t> data(KCP_MAX_DATA_SIZE);

        size_t buflen;
        socklen_t addrlen;
        sockaddr* addr = nullptr;
        uint8_t *buf = nullptr;

        while(state_ == State::Runing) {
            if (n = ::recvmmsg(sockfd, msgs, MSG_LEN, MSG_WAITFORONE, nullptr), n < 0)
                continue;

            for (i = 0; i < n; i++) {
                buf = bufs[i];
                buflen = msgs[i].msg_len;
                if (buflen < KCP_HEAD_SIZE)
                    continue;

                addrlen = msgs[i].msg_hdr.msg_namelen;
                addr = &addrs[i];

                conv = xq::tools::to_le_u32(*(uint32_t*)buf);
                itr = sess_map_.find(conv);
                if (itr == sess_map_.end()) {
                    s = KcpSess::create(conv, &KcpListener::_udp_output);
                    if (event_.on_connected(s.get()) < 0)
                        continue;

                    pair = add_sess(conv, s);
                    assert(pair.second);
                    itr = pair.first;
                }
                sess =  itr->second.get();
                if (sess->change(sockfd, addr, addrlen)) {
                    // TODO reconnected
                }

                if (sess->input(buf, buflen) < 0) {
                    remove_sess(itr);
                    continue;
                }

                n = sess->recv(&data[0], KCP_MAX_DATA_SIZE);
                if (n <= 0)
                    continue;

                sess->flush();

                if (event_.on_message(sess, &data[0], n) < 0)
                    remove_sess(itr);
            }
        }
    }
#endif

    std::string host_;
    State state_;
    uint32_t timeout_;
    uint32_t nthread_;

    std::thread update_thr_;
    std::vector<SOCKET> ufds_;
    std::vector<std::thread> recv_pool_;
    SessMap sess_map_;
    TEvent event_;

    KcpListener(const KcpListener&) = delete;
    KcpListener& operator=(const KcpListener&) = delete;
}; // class KcpListener;


} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__
