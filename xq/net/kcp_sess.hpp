#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess final {
public:
    static tools::ObjectPool<KcpSess>* pool() {
        return tools::ObjectPool<KcpSess>::Instance();
    }

    KcpSess(uint32_t conv = 0xFFFFFFFF)
        : ufd_(INVALID_SOCKET)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({ 0,{0}})
        , addrlen_(sizeof(addr_))
        , kcp_(new Kcp(conv, this))  {
        kcp_->nodelay(1, 20, 2, 1);
    }

    ~KcpSess() {
        delete kcp_;
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

    uint32_t get_conv() {
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
        int rzt = kcp_->input(data, size);
        if (rzt == 0) {
            kcp_->flush();
            _sendmsg();
        }
            return rzt;
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

    size_t nrcv_que() {
        std::lock_guard<std::mutex> lk(kmtx_);
        return kcp_->nrcv_que();
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
    void _sendmsg() {
        size_t n = segs_.size();

        if (n == 0) {
            return;
        }

        iovec *iovs = new iovec[n], *iov;
        TxSeg *seg;

        msghdr msg;
        ::memset(&msg, 0, sizeof(msg));

        msg.msg_name = &addr_;
        msg.msg_namelen = addrlen_;

        msg.msg_iov = iovs;
        msg.msg_iovlen = n;

        for (size_t i = 0; i < n; i++) {
            seg = segs_[i];
            iov = &iovs[i];
            iov->iov_base = seg->data;
            iov->iov_len = seg->len;
        }

        if (::sendmsg(ufd_, &msg, 0) < 0) {
            //TODO: ...
            printf("sendmsg failed: %d\n", error());
        }

        delete[] iovs;

        for (auto seg: segs_) {
            TxSeg::pool()->put(seg);
        }

        segs_.clear();
    }

    bool _addr_changed(const sockaddr* addr, socklen_t addrlen) {
        bool res = false;
        std::lock_guard<std::mutex> lk(amtx_);
        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
            res = true;
        }

        if (::memcmp(&addr_, addr, addrlen_)) {
            ::memcpy(&addr_, addr, addrlen_);
            if (!res) {
                res = true;
            }
        }

        return res;
    }

    SOCKET ufd_;

    int64_t time_ms_;
    int64_t last_ms_;
    size_t qidx_;
    sockaddr addr_;
    socklen_t addrlen_;

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
