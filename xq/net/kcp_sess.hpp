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
        int rzt = kcp_->input((char*)data, size);
        if (rzt == 0) {
            kcp_->flush();
        }
        return rzt;
    }

    int recv(uint8_t* buf, int len) {
        std::lock_guard<std::mutex> lk(kmtx_);
        return kcp_->recv((char *)buf, len);
    }

    int send(const uint8_t* buf, int len) {
        std::lock_guard<std::mutex> lk(kmtx_);
        int rzt = kcp_->send((char *)buf, len);
        if (rzt == 0) {
            kcp_->flush();
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
        return ufd_;
    }

    void update(int64_t now_ms) {
        do {
            if (ufd_ == INVALID_SOCKET) {
                break;
            }

            bool res;
            {
                std::lock_guard<std::mutex> lk(tmtx_);
                res = now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT;
            }

            std::lock_guard<std::mutex> lk(kmtx_);
            if (res) {
                kcp_->reset();
                break;
            }

            kcp_->update((uint32_t)(now_ms - time_ms_));
        } while (0);
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

private:
    KcpSess(uint32_t conv)
        : ufd_(INVALID_SOCKET)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({ 0,{0}})
        , addrlen_(sizeof(addr_))
        , kcp_(new Kcp(conv, this)) {
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
    std::mutex kmtx_;
    std::mutex tmtx_;
    std::mutex amtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
