#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"
#include "net/net.hpp"
#include "tools/tools.hpp"

namespace xq {
namespace net {

class KcpSess {
public:
    friend class KcpListener;

    ~KcpSess() {
#ifndef WIN32
        for (size_t i = 0; i < IO_BLOCK_SIZE; i++) {
            delete[](uint8_t*)msg_.msg_iov[i].iov_base;
        }
        delete[] msg_.msg_iov;
#endif // !WIN32

        if (kcp_) {
            delete kcp_;
        }
    }

    std::string get_remote() const {
        return remote_;
    }

    SOCKET get_ufd() const {
        return ufd_;
    }

    uint32_t get_conv() const {
        return kcp_->get_conv();
    }

    uint32_t get_que_num() const {
        return que_num_;
    }

    int64_t get_last_time() const {
        return last_ms_;
    }

    int send(const uint8_t* buf, int len) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        int rzt = kcp_->send(buf, len);
        if (rzt == 0) {
            kcp_->flush();
            _sendmsg();
        }
        return rzt;
    }

private:
    explicit KcpSess(uint32_t conv)
        : ufd_(INVALID_SOCKET)
        , que_num_(~0)
        , time_ms_(0)
        , last_ms_(0)
        , raddr_({ 0,{0}})
        , raddrlen_(sizeof(raddr_))
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

    void _set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        kcp_->set_output(output);
    }

    void _set_que_num(uint32_t qidx) {
        que_num_ = qidx;
    }

    /// <summary>
    /// 检查该会话地址是否改变.
    /// </summary>
    /// <param name="ufd"></param>
    /// <param name="addr"></param>
    /// <param name="addrlen"></param>
    /// <returns>返回 0 时, 未改变; 返回 1 时, 新连接; 返回 2 时, 重新连接</returns>
    int _check(SOCKET ufd, sockaddr *addr, socklen_t addrlen) {
        int res = 0;
        bool changed = false;

        if (ufd != ufd_) {
            ufd_ = ufd;
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
            changed = ::memcmp(addr, &raddr_, addrlen) || addrlen != raddrlen_;
            if (changed) {
                ::memcpy(&raddr_, addr, addrlen);
                raddrlen_ = addrlen;
                remote_ = xq::net::addr2str(&raddr_);

        #ifndef WIN32
                msg_.msg_iovlen = 0;
                msg_.msg_name = &raddr_;
                msg_.msg_namelen = raddrlen_;
        #endif // !WIN32

                res++;
            }
        }

        if (changed) {
            {
                std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
                kcp_->reset();
            }

            {
                std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
                if (last_ms_ > 0) {
                    res++;
                }
                time_ms_ = xq::tools::now_milli();
            }
        }

        return res;
    }

    void _set_nodelay(int nodelay, int interval, int resend, int nc) {
        kcp_->nodelay(nodelay, interval, resend, nc);
    }

    int _input(const uint8_t* data, long size) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        return kcp_->input(data, size);
    }

    int _recv(uint8_t* buf, int len) {
        std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
        return kcp_->recv(buf, len);
    }

    bool _update(int64_t now_ms) {
        {
            std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
            if (ufd_ == INVALID_SOCKET || last_ms_ == 0) {
                return false;
            }

            if (now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
                time_ms_ = last_ms_ = 0;
                que_num_ = ~0;
                return false;
            }
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(kcp_mtx_);
            kcp_->update((uint32_t)(now_ms - time_ms_));
            _sendmsg();
        }

        return true;
    }

    void _set_last_ms(int64_t now_ms) {
        std::lock_guard<xq::tools::SpinLock> lk(time_mtx_);
        last_ms_ = now_ms;
    }

    void _append_data(uint8_t *data, size_t len) {
#ifndef WIN32
        iovec* iov = &msg_.msg_iov[msg_.msg_iovlen++];
        ::memcpy((uint8_t*)iov->iov_base, data, len);
        iov->iov_len = len;

        if (msg_.msg_iovlen == IO_BLOCK_SIZE >> 1) {
            _sendmsg();
        }
#else
        std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
        if (::sendto(ufd_, (const char*)data, len, 0, &raddr_, raddrlen_)) {
            printf("sendto failed: %d\n", error());
            // TODO: ...
        }
#endif // !WIN32
    }

    void _sendmsg() {
#ifndef WIN32
        if (msg_.msg_iovlen == 0) {
            return;
        }

        {
            std::lock_guard<xq::tools::SpinLock> lk(addr_mtx_);
            if (::sendmsg(ufd_, &msg_, 0) < 0) {
                //TODO: ...
                printf("sendmsg failed: %d\n", error());
            }
        }

        msg_.msg_iovlen = 0;
#endif // !WIN32
    }

    SOCKET ufd_;

    uint32_t que_num_;
    int64_t time_ms_;
    int64_t last_ms_;
    sockaddr raddr_;
    socklen_t raddrlen_;

#ifndef WIN32
    msghdr msg_;
#endif // !WIN32

    Kcp* kcp_;
    std::string remote_;

    xq::tools::SpinLock kcp_mtx_;
    xq::tools::SpinLock time_mtx_;
    xq::tools::SpinLock addr_mtx_;

    KcpSess(const KcpSess&) = delete;
    KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
