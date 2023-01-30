#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess final {
public:
    static xq::tools::ObjectPool<KcpSess>* pool() {
        return xq::tools::ObjectPool<KcpSess>::Instance();
    }

    static xq::tools::Map<std::string, KcpSess*>& sessions() {
        static xq::tools::Map<std::string, KcpSess*> m_;
        return m_;
    }

    explicit KcpSess(uint32_t conv = ~(0))
        : ufd_(INVALID_SOCKET)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({ 0,{0}})
        , addrlen_(sizeof(addr_))
#ifndef WIN32
        , tx_iovs_(new iovec[IO_BLOCK_SIZE])
#endif // !WIN32
        , kcp_(new Kcp(conv, this))  {
        kcp_->nodelay(1, KCP_UPDATE_MS, 2, 0);
    }

    ~KcpSess() {
#ifndef WIN32
        if (tx_iovs_) {
            delete[] tx_iovs_;
    }
#endif // !WIN32

        if (kcp_) {
            delete kcp_;
        }
    }

    std::pair<sockaddr*, socklen_t> addr() {
        return std::make_pair(&addr_, addrlen_);
    }

    void set(uint32_t conv, SOCKET ufd, sockaddr *addr, socklen_t addrlen, const std::string &remote, size_t qidx, int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        kcp_->reset();
        kcp_->set_conv(conv);
        kcp_->set_output(output);
        last_ms_ = time_ms_ = xq::tools::now_milli();
        if (ufd != ufd_) {
            ufd_ = ufd;
        }

        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
        remote_ = remote;
        qidx_ = qidx;
    }

    uint32_t get_conv() const {
        return kcp_->get_conv();
    }

    uint32_t get_que_idx() const {
        return qidx_;
    }

    void nodelay(int nodelay, int interval, int resend, int nc) {
        std::lock_guard<std::mutex> lk(kmtx_);
        kcp_->nodelay(nodelay, interval, resend, nc);
    }

    int input(const uint8_t* data, long size) {
        std::lock_guard<std::mutex> lk(kmtx_);
        return kcp_->input(data, size);
    }

    int recv(uint8_t* buf, int len) {
        std::lock_guard<std::mutex> lk(kmtx_);
        return kcp_->recv(buf, len);
    }

    int send(const uint8_t* buf, int len) {
        std::lock_guard<std::mutex> lk(kmtx_);
        int rzt = kcp_->send(buf, len);
        if (rzt == 0) {
            kcp_->flush();
            _sendmsg();
        }
        return rzt;
    }

    std::string remote() const {
        return remote_;
    }

    SOCKET ufd() const {
        return ufd_;
    }

    bool update(int64_t now_ms) {
        {
            std::lock_guard<std::mutex> lk(tmtx_);
            if (ufd_ == INVALID_SOCKET || last_ms_ == 0) {
                return true;
            }

            if (now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
                last_ms_ = 0;
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> lk(kmtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
            _sendmsg();
        }

        return true;
    }

    void set_last_ms(int64_t now_ms) {
        std::lock_guard<std::mutex> lk(tmtx_);
        last_ms_ = now_ms;
    }

    void push_tx_seg(TxSeg* seg) {
        segs_.push_back(seg);
    }

private:
#ifndef WIN32
    void _sendmsg() {
        size_t n = segs_.size();

        if (n == 0) {
            return;
        }

        iovec *iov;
        TxSeg* seg;

        msghdr msg;
        ::memset(&msg, 0, sizeof(msg));

        msg.msg_name = &addr_;
        msg.msg_namelen = addrlen_;

        msg.msg_iov = tx_iovs_;
        msg.msg_iovlen = n;

        for (size_t i = 0; i < n; i++) {
            seg = segs_[i];
            iov = &tx_iovs_[i];
            iov->iov_base = seg->data;
            iov->iov_len = seg->len;
        }

        if (::sendmsg(ufd_, &msg, 0) < 0) {
            //TODO: ...
            printf("sendmsg failed: %d\n", error());
        }

        for (auto seg : segs_) {
            TxSeg::pool()->put(seg);
        }

        segs_.clear();
    }
#else
    void _sendmsg() {
        size_t n = segs_.size();

        if (n == 0) {
            return;
        }

        for (auto& seg : segs_) {
            if (::sendto(ufd_, (const char*)seg->data, seg->len, 0, &addr_, addrlen_)) {
                printf("sendto failed: %d\n", error());
                break;
            }
        }

        segs_.clear();
    }
#endif // !WIN32

    SOCKET ufd_;

    int64_t time_ms_;
    int64_t last_ms_;
    size_t qidx_;
    sockaddr addr_;
    socklen_t addrlen_;

#ifndef WIN32
    iovec* tx_iovs_;
#endif // !WIN32

    Kcp* kcp_;

    std::vector<TxSeg*> segs_;

    std::string remote_;

    std::mutex kmtx_;
    std::mutex tmtx_;
    std::mutex amtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
