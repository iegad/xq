#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess final {
public:
    explicit KcpSess(uint32_t conv)
        : ufd_(INVALID_SOCKET)
        , qidx_(0)
        , time_ms_(0)
        , last_ms_(0)
        , addr_({ 0,{0}})
        , addrlen_(sizeof(addr_))
        , kcp_(new Kcp(conv, this)) {
#ifndef WIN32
        ::memset(&msg_, 0, sizeof(msg_));
        iovec* tmp = new iovec[IO_BLOCK_SIZE];
        msg_.msg_iov = tmp;
        for (size_t i = 0; i < IO_BLOCK_SIZE; i++) {
            tmp[i].iov_base = new uint8_t[KCP_MTU];
            tmp[i].iov_len = KCP_MTU;
        }
#endif // !WIN32
    }

    ~KcpSess() {
#ifndef WIN32
        for (size_t i = 0; i < IO_BLOCK_SIZE; i++) {
            delete[] (uint8_t*)msg_.msg_iov[i].iov_base;
        }
        delete[] msg_.msg_iov;
#endif // !WIN32

        if (kcp_) {
            delete kcp_;
        }
    }

    std::pair<sockaddr*, socklen_t> addr() {
        return std::make_pair(&addr_, addrlen_);
    }

    void set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        kcp_->set_output(output);
    }

    void set_que_idx(uint32_t qidx) {
        qidx_ = qidx;
    }

    bool check(SOCKET ufd, sockaddr *addr, socklen_t addrlen) {
        if (ufd != ufd_) {
            ufd_ = ufd;
        }

        if (addrlen != addrlen_ || ::memcmp(addr, &addr_, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
            addrlen_ = addrlen;
            remote_ = xq::net::addr2str(&addr_);

            kcp_->reset();
            last_ms_ = time_ms_ = xq::tools::now_milli();

#ifndef WIN32
            msg_.msg_iovlen = 0;
            msg_.msg_name = &addr_;
            msg_.msg_namelen = addrlen_;
#endif // !WIN32

            return true;
        }

        return false;
    }

    uint32_t get_conv() const {
        return kcp_->get_conv();
    }

    uint32_t get_que_idx() const {
        return qidx_;
    }

    void set_nodelay(int nodelay, int interval, int resend, int nc) {
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
                return false;
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

    void append_data(uint8_t *data, size_t len) {
#ifndef WIN32
        iovec* iov = &msg_.msg_iov[msg_.msg_iovlen++];
        ::memcpy((uint8_t*)iov->iov_base, data, len);
        iov->iov_len = len;

        if (msg_.msg_iovlen == IO_BLOCK_SIZE >> 1) {
            _sendmsg();
        }
#else
        if (::sendto(ufd_, (const char*)data, len, 0, &addr_, addrlen_)) {
            printf("sendto failed: %d\n", error());
            // TODO: ...
        }
#endif // !WIN32
    }

private:
    void _sendmsg() {
#ifndef WIN32
        if (msg_.msg_iovlen == 0) {
            return;
        }

        if (::sendmsg(ufd_, &msg_, 0) < 0) {
            //TODO: ...
            printf("sendmsg failed: %d\n", error());
        }
        msg_.msg_iovlen = 0;
#endif // !WIN32
    }


    SOCKET ufd_;

    uint32_t qidx_;
    int64_t time_ms_;
    int64_t last_ms_;
    sockaddr addr_;
    socklen_t addrlen_;

#ifndef WIN32
    msghdr msg_;
#endif // !WIN32

    Kcp* kcp_;
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
