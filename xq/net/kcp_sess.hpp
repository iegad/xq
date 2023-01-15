#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess: public Kcp {
public:
    typedef std::shared_ptr<KcpSess>  Ptr;

    static Ptr create(uint32_t conv) {
        return Ptr(new KcpSess(conv));
    }

    std::pair<sockaddr*, socklen_t> addr() {
        return std::make_pair(&addr_, addrlen_);
    }

    void set_ufd(SOCKET ufd) {
        if (ufd_ != ufd) {
            ufd_ = ufd;
        }
    }

    std::string remote() {
        std::string rzt;
        char buf[100] = {0};

        switch (addr_.sa_family) {

        case AF_INET: {
            sockaddr_in *addr = (sockaddr_in*)&addr_;
            assert(::inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) && "inet_ntop failed");
            rzt = std::string(buf) + ":" + std::to_string(ntohs(addr->sin_port));
        } break;

        case AF_INET6: {
            sockaddr_in6 *addr = (sockaddr_in6*)&addr_;
            assert(::inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf)) && "inet_ntop failed");
            rzt = std::string(buf) + ":" +  std::to_string(ntohs(addr->sin6_port));
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

            if (now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
                reset();
                break;
            }

            Kcp::update((uint32_t)(now_ms - time_ms_));
        } while(0);
    }

    void set_last_ms(int64_t now_ms) {
        last_ms_ = now_ms;
    }

    bool check_new(const sockaddr* addr, socklen_t addrlen) {
        bool res = addrlen != addrlen_;
        if (res || ::memcmp(addr, &addr_, addrlen)) {
            if (res) {
                addrlen_ = addrlen;
            }
            
            ::memcpy(&addr_, addr, addrlen_);
            reset();
            last_ms_ = time_ms_ = xq::tools::now_milli();
            return true;
        }

        return false;
    }

private:
    KcpSess(uint32_t conv)
        : Kcp(conv, this)
        , ufd_(INVALID_SOCKET)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({0,{0}})
        , addrlen_(sizeof(addr_)) {
    }

    SOCKET ufd_;
    int64_t time_ms_;
    int64_t last_ms_;
    sockaddr addr_;
    socklen_t addrlen_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
