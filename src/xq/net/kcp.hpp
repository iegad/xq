#ifndef __XQ_NET_KCP__
#define __XQ_NET_KCP__


#include <memory>
#include <mutex>
#include <unordered_map>

#include "xq/net/net.hpp"
#include "xq/third/ikcp.h"


namespace xq {
namespace net {


// ------------------------------------------------------------------------ Kcp ------------------------------------------------------------------------

/// @brief KCP协议 C++封装
class Kcp final {
public:


    /// @brief KCP 消息头
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


    /// @brief 构建函数
    /// @param conv kcp conv
    /// @param user 附加参数, 该框架中为: KcpSess / KcpHost
    explicit Kcp(uint32_t conv, void* user)
        : kcp_(::ikcp_create(conv, user)) {
        ::ikcp_nodelay(kcp_, 1, 5, 3, 0);
        ::ikcp_setmtu(kcp_, KCP_MTU);
        ::ikcp_wndsize(kcp_, KCP_WND, KCP_WND);
    }


    ~Kcp() {
        if (kcp_) {
            ::ikcp_release(kcp_);
        }
    }


    /// @brief 从原始IO流中获取conv
    /// @param raw 原始IO数据
    /// @return 返回原始IO数据中的 conv
    static uint32_t get_conv(const void *raw) {
        return ::ikcp_getconv(raw);
    }


    /// @brief  解码消息头
    /// @param raw  原始IO流
    /// @param head OUT KCP消息头
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


    /// @brief 设置output回调
    /// @param output 有效的回调函数
    void set_output(int (*output)(const uint8_t* buf, size_t len, ikcpcb* kcp, void* user)) {
        kcp_->output = output;
    }


    /// @brief 获取当前Kcp conv
    uint32_t conv() const {
        return kcp_->conv;
    }


    /// @brief 从kcp rcv_que 中接收数据
    /// @param buf  OUT数据缓冲区
    /// @param len  数据缓冲区长度
    /// @return 成功返回0, 否则返回!0
    int recv(uint8_t* buf, int len) {
        return ::ikcp_recv(kcp_, buf, len);
    }


    /// @brief 发送数据, 该发送仅把数据放入kcp发送队列
    /// @param buf 
    /// @param len 
    /// @return 成功返回0, 否则返回!0
    int send(const uint8_t* buf, int len) {
        return ::ikcp_send(kcp_, buf, len);
    }


    /// @brief kcp update
    /// @param current 当前kcp时间(毫秒)
    void update(uint32_t current) {
        ::ikcp_update(kcp_, current);
    }


    /// @brief 将原始IO流转换为kcp数据
    /// @param data 原始IO数据
    /// @param size 原始数据长度
    /// @return 成功返回0, 否则返回!0
    int input(const uint8_t* data, long size) {
        return ::ikcp_input(kcp_, data, size);
    }


    /// @brief 将发送缓冲区的数据给 回调函数(output)处理
    void flush() {
        ::ikcp_flush(kcp_);
    }


    /// @brief 重置KCP
    void reset(uint32_t conv) {
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

        kcp_->conv = conv;
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


#endif // !__XQ_NET_KCP__
