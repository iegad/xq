#ifndef __NET_KCP__
#define __NET_KCP__

#include "third/ikcp.h"
#include "net/net.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace xq {
namespace net {

/// <summary>
/// State: KCP 服务器状态
/// </summary>
enum class State {
    Stopped,    // 停止
    Stopping,   // 停止中
    Running     // 运行
};

class Kcp final {
public:
    struct Head {
        uint32_t conv;
        uint8_t  cmd;
        uint8_t  frg;
        uint16_t wnd;
        uint32_t ts;
        uint32_t sn;
        uint32_t una;
        uint32_t len;
    };

    explicit Kcp(uint32_t conv, void* user)
        : kcp_(::ikcp_create(conv, user)) {
        ::ikcp_setmtu(kcp_, KCP_MTU);
        ::ikcp_wndsize(kcp_, KCP_WND, KCP_WND);
        kcp_->updated = 1;
    }

    static uint32_t get_conv(const void *raw) {
        return ::ikcp_getconv(raw);
    }

    ~Kcp() {
        if (kcp_) {
            ::ikcp_release(kcp_);
        }
    }

    void set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
        kcp_->output = output;
    }

    uint32_t get_conv() const {
        return kcp_->conv;
    }

    int recv(uint8_t* buf, int len) {
        return ::ikcp_recv(kcp_, (char *)buf, len);
    }

    int send(const uint8_t* buf, int len) {
        return ::ikcp_send(kcp_, (const char *)buf, len);
    }

    void update(uint32_t current) {
        ::ikcp_update(kcp_, current);
    }

    int input(const uint8_t* data, long size) {
        return ::ikcp_input(kcp_, (const char *)data, size);
    }

    bool state() {
        return kcp_->state == 0;
    }

    void flush() {
        ::ikcp_flush(kcp_);
    }

    int nodelay(int nodelay, int interval, int resend, int nc) {
        return ::ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
    }

    static void decode_head(const uint8_t* raw, Head *head) {
        const uint8_t*p = raw;
        head->conv = *(uint32_t*)p;
        p += 4;
        head->cmd = *(uint8_t*)p;
        p += 1;
        head->frg = *(uint8_t*)p;
        p += 1;
        head->wnd = *(uint16_t*)p;
        p += 2;
        head->ts = *(uint32_t*)p;
        p += 4;
        head->sn = *(uint32_t*)p;
        p += 4;
        head->una = *(uint32_t*)p;
        p += 4;
        head->len = *(uint32_t*)p;
    }

    void reset() {
        IKCPSEG* seg;
        while (!IQUEUE_IS_EMPTY(&kcp_->snd_buf)) {
            seg = IQUEUE_ENTRY(kcp_->snd_buf.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&kcp_->rcv_buf)) {
            seg = IQUEUE_ENTRY(kcp_->rcv_buf.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&kcp_->snd_queue)) {
            seg = IQUEUE_ENTRY(kcp_->snd_queue.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&kcp_->rcv_queue)) {
            seg = IQUEUE_ENTRY(kcp_->rcv_queue.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }

        kcp_->snd_una = 0;
        kcp_->snd_nxt = 0;
        kcp_->rcv_nxt = 0;
        kcp_->ts_recent = 0;
        kcp_->ts_lastack = 0;
        kcp_->ts_probe = 0;
        kcp_->probe_wait = 0;
        kcp_->cwnd = 0;
        kcp_->incr = 0;
        kcp_->probe = 0;
        kcp_->stream = 0;

        kcp_->nrcv_buf = 0;
        kcp_->nsnd_buf = 0;
        kcp_->nrcv_que = 0;
        kcp_->nsnd_que = 0;
        kcp_->state = 0;
        kcp_->ackblock = 0;
        kcp_->ackcount = 0;
        kcp_->rx_srtt = 0;
        kcp_->rx_rttval = 0;
        kcp_->current = 0;
        kcp_->nodelay = 0;
        kcp_->updated = 0;
        kcp_->logmask = 0;
        kcp_->fastresend = 0;
        kcp_->nocwnd = 0;
        kcp_->xmit = 0;
    }

private:
    IKCPCB* kcp_;

    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
}; // class Kcp

} // namespace net
} // namespace xq


#endif // !__NET_KCP__
