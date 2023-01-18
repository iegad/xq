#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess final {
public:
    typedef std::shared_ptr<KcpSess>  Ptr;

    static Ptr create(uint32_t conv) {
        return Ptr(new KcpSess(conv));
    }

    ~KcpSess() {
        delete kcp_;
    }

    std::pair<sockaddr*, socklen_t> addr() {
        return std::make_pair(&addr_, addrlen_);
    }

    uint32_t conv() {
        return kcp_->conv();
    }

    void set_ufd(SOCKET ufd) {
        if (ufd_ != ufd) {
            ufd_ = ufd;
        }
    }

    void nodelay(int nodelay, int interval, int resend, int nc) {
        std::lock_guard<std::mutex> lk(kmtx_);
        kcp_->nodelay(nodelay, interval, resend, nc);
    }

    void set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        std::lock_guard<std::mutex> lk(kmtx_);
        kcp_->set_output(output);
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

    std::string remote() {
        std::string rzt;
        char buf[100] = { 0 };

        std::lock_guard<std::mutex> lk(amtx_);
        switch (addr_.sa_family) {

        case AF_INET: {
            sockaddr_in* addr = (sockaddr_in*)&addr_;
            assert(::inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) && "inet_ntop failed");
            rzt = std::string(buf) + ":" + std::to_string(ntohs(addr->sin_port));
        } break;

        case AF_INET6: {
            sockaddr_in6* addr = (sockaddr_in6*)&addr_;
            assert(::inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf)) && "inet_ntop failed");
            rzt = std::string(buf) + ":" + std::to_string(ntohs(addr->sin6_port));
        } break;
        } // switch (addr_.sa_family);

        return rzt;
    }

    SOCKET ufd() {
        std::lock_guard<std::mutex> lk(tmtx_);
        return ufd_;
    }

    void update(int64_t now_ms) {
        {
            std::lock_guard<std::mutex> lk(tmtx_);
            if (ufd_ == INVALID_SOCKET || now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lk(kmtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
            _sendmsg();
        }
    }

    void set_last_ms(int64_t now_ms) {
        std::lock_guard<std::mutex> lk(tmtx_);
        last_ms_ = now_ms;
    }

    bool check_new(const sockaddr* addr, socklen_t addrlen) {
        if (_addr_changed(addr, addrlen)) {
            {
                std::lock_guard<std::mutex> lk(kmtx_);
                kcp_->reset();
            }

            {
                std::lock_guard<std::mutex> lk(tmtx_);
                last_ms_ = time_ms_ = xq::tools::now_milli();
            }

            return true;
        }

        return false;
    }

    void push_tx_seg(TxSeg* seg) {
        segs_.push_back(seg);
    }

private:
    KcpSess(uint32_t conv)
        : ufd_(INVALID_SOCKET)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({ 0,{0}})
        , addrlen_(sizeof(addr_))
        , kcp_(new Kcp(conv, this))  {
    }

    void _sendmsg() {
        size_t n = segs_.size();
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
    sockaddr addr_;
    socklen_t addrlen_;

    Kcp* kcp_;

    std::vector<TxSeg*> segs_;

    std::mutex kmtx_;
    std::mutex tmtx_;
    std::mutex amtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
