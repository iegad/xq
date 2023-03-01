#ifndef __XQ_NET_KCP__
#define __XQ_NET_KCP__


#include <memory>
#include <mutex>
#include <unordered_map>

#include "xq/net/net.hpp"


namespace xq {
namespace net {


// ------------------------------------------------------------------------ Kcp ------------------------------------------------------------------------

#define IKCP_RTO_NDL		30		// no delay min rto
#define IKCP_RTO_MIN		100		// normal min rto
#define IKCP_RTO_DEF		200
#define IKCP_RTO_MAX		60000
#define IKCP_CMD_PUSH		81	// cmd: push data
#define IKCP_CMD_ACK		82	// cmd: ack
#define IKCP_CMD_WASK		83	// cmd: window probe (ask)
#define IKCP_CMD_WINS		84	// cmd: window size (tell)
#define IKCP_ASK_SEND		1	// need to send IKCP_CMD_WASK
#define IKCP_ASK_TELL		2	// need to send IKCP_CMD_WINS
#define IKCP_WND_SND		32
#define IKCP_WND_RCV		128      // must >= max fragment size
#define IKCP_MTU_DEF		1400
#define IKCP_ACK_FAST		3
#define IKCP_INTERVAL		100
#define IKCP_OVERHEAD		24
#define IKCP_DEADLINK		20
#define IKCP_THRESH_INIT	2
#define IKCP_THRESH_MIN		2
#define IKCP_PROBE_INIT		7000		// 7 secs to probe window size
#define IKCP_PROBE_LIMIT	120000	// up to 120 secs to probe window
#define IKCP_FASTACK_LIMIT	5		// max times to trigger fastack




//=====================================================================
// QUEUE DEFINITION                                                  
//=====================================================================

struct IQUEUEHEAD {
    struct IQUEUEHEAD* next, * prev;
};

typedef struct IQUEUEHEAD iqueue_head;


//---------------------------------------------------------------------
// queue init                                                         
//---------------------------------------------------------------------
#define IQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
	struct IQUEUEHEAD name = IQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define IQUEUE_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation                     
//---------------------------------------------------------------------
#define IQUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define IQUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define IQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define IQUEUE_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define IQUEUE_DEL_INIT(entry) do { \
	IQUEUE_DEL(entry); IQUEUE_INIT(entry); } while (0)

#define IQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define iqueue_init		IQUEUE_INIT
#define iqueue_entry	IQUEUE_ENTRY
#define iqueue_add		IQUEUE_ADD
#define iqueue_add_tail	IQUEUE_ADD_TAIL
#define iqueue_del		IQUEUE_DEL
#define iqueue_del_init	IQUEUE_DEL_INIT
#define iqueue_is_empty IQUEUE_IS_EMPTY

#define IQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = iqueue_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = iqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define iqueue_foreach(iterator, head, TYPE, MEMBER) \
	IQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define iqueue_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )


#define __iqueue_splice(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define iqueue_splice(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice(list, head); } while (0)

#define iqueue_splice_init(list, head) do {	\
	iqueue_splice(list, head);	iqueue_init(list); } while (0)


/// @brief KCP协议 C++封装
class Kcp final {
public:
    struct IKCPSEG
    {
        struct IQUEUEHEAD node;
        uint32_t conv;
        uint32_t cmd;
        uint32_t frg;
        uint32_t wnd;
        uint32_t ts;
        uint32_t sn;
        uint32_t una;
        uint32_t len;
        uint32_t resendts;
        uint32_t rto;
        uint32_t fastack;
        uint32_t xmit;
        uint8_t data[1];
    };


    /// @brief 构建函数
    /// @param conv kcp conv
    /// @param user 附加参数, 该框架中为: KcpSess / KcpHost
    explicit Kcp(uint32_t conv, void* user, int (*output)(const uint8_t* buf, size_t len, void* user))
        : conv_(conv)
        , mtu_(KCP_MTU)
        , mss_(KCP_MTU - KCP_HEAD_SIZE)
        , state_(0)
        , snd_una_(0)
        , snd_nxt_(0)
        , rcv_nxt_(0)
        , ssthresh_(IKCP_THRESH_INIT)
        , rx_rttval_(0)
        , rx_srtt_(0)
        , rx_rto_(IKCP_RTO_DEF)
        , rx_minrto_(IKCP_RTO_MIN)
        , snd_wnd_(KCP_WND)
        , rcv_wnd_(KCP_WND)
        , rmt_wnd_(KCP_WND)
        , cwnd_(1)
        , probe_(0)
        , current_(0)
        , interval_(KCP_UPDATE_MS / 2)
        , ts_flush_(KCP_UPDATE_MS / 2)
        , xmit_(0)
        , nrcv_buf_(0)
        , nsnd_buf_(0)
        , nrcv_que_(0)
        , nsnd_que_(0)
        , nodelay_(1)
        , updated_(0)
        , ts_probe_(0)
        , probe_wait_(0)
        , dead_link_(IKCP_DEADLINK)
        , incr_(0)
        , acklist_(nullptr)
        , ackcount_(0)
        , ackblock_(0)
        , user_(user)
        , buffer_((uint8_t*)::malloc((KCP_MTU + KCP_HEAD_SIZE) * 3))
        , fastresend_(3)
        , fastlimit_(IKCP_FASTACK_LIMIT)
        , nocwnd_(0)
        , output_(nullptr) {
        iqueue_init(&snd_queue_);
        iqueue_init(&rcv_queue_);
        iqueue_init(&snd_buf_);
        iqueue_init(&rcv_buf_);
        output_ = output;
    }


    ~Kcp() {
        IKCPSEG* seg;
        while (!iqueue_is_empty(&snd_buf_)) {
            seg = iqueue_entry(snd_buf_.next, IKCPSEG, node);
            iqueue_del(&seg->node);
            ::free(seg);
        }
        while (!iqueue_is_empty(&rcv_buf_)) {
            seg = iqueue_entry(rcv_buf_.next, IKCPSEG, node);
            iqueue_del(&seg->node);
            ::free(seg);
        }
        while (!iqueue_is_empty(&snd_queue_)) {
            seg = iqueue_entry(snd_queue_.next, IKCPSEG, node);
            iqueue_del(&seg->node);
            ::free(seg);
        }
        while (!iqueue_is_empty(&rcv_queue_)) {
            seg = iqueue_entry(rcv_queue_.next, IKCPSEG, node);
            iqueue_del(&seg->node);
            ::free(seg);
        }
        if (buffer_) {
            ::free(buffer_);
        }
        if (acklist_) {
            ::free(acklist_);
        }
    }


    /// @brief 从原始IO流中获取conv
    /// @param raw 原始IO数据
    /// @return 返回原始IO数据中的 conv
    static uint32_t get_conv(const void* raw) {
        uint32_t conv;
        _decode32u((const uint8_t*)raw, &conv);
        return conv;

    }


    /// @brief 获取当前Kcp conv
    uint32_t conv() const {
        return conv_;
    }


    /// @brief 从kcp rcv_que 中接收数据
    /// @param buf  OUT数据缓冲区
    /// @param len  数据缓冲区长度
    /// @return 成功返回0, 否则返回!0
    int recv(uint8_t* buf, size_t len) {
        assert(buf);

        struct IQUEUEHEAD* p;
        int recover = 0;
        IKCPSEG* seg;

        if (iqueue_is_empty(&rcv_queue_)) {
            return -1;
        }

        int peeksize = _peeksize();

        if (peeksize < 0) {
            return -2;
        }

        if (peeksize > (int)len) {
            return -3;
        }

        if (nrcv_que_ >= rcv_wnd_) {
            recover = 1;
        }

        // merge fragment
        for (len = 0, p = rcv_queue_.next; p != &rcv_queue_; ) {
            int fragment;
            seg = iqueue_entry(p, IKCPSEG, node);
            p = p->next;

            memcpy(buf, seg->data, seg->len);
            buf += seg->len;

            len += seg->len;
            fragment = seg->frg;

            iqueue_del(&seg->node);
            ::free(seg);
            nrcv_que_--;

            if (fragment == 0) {
                break;
            }
        }

        assert((int)len == peeksize);

        // move available data from rcv_buf -> rcv_queue
        while (!iqueue_is_empty(&rcv_buf_)) {
            seg = iqueue_entry(rcv_buf_.next, IKCPSEG, node);
            if (seg->sn == rcv_nxt_ && nrcv_que_ < rcv_wnd_) {
                iqueue_del(&seg->node);
                nrcv_buf_--;
                iqueue_add_tail(&seg->node, &rcv_queue_);
                nrcv_que_++;
                rcv_nxt_++;
            }
            else {
                break;
            }
        }

        // fast recover
        if (nrcv_que_ < rcv_wnd_ && recover) {
            // ready to send back IKCP_CMD_WINS in ikcp_flush
            // tell remote my window size
            probe_ |= IKCP_ASK_TELL;
        }

        return len;
    }


    /// @brief 发送数据, 该发送仅把数据放入kcp发送队列
    /// @param buf 
    /// @param len 
    /// @return 成功返回0, 否则返回!0
    int send(const uint8_t* buf, size_t len) {
        IKCPSEG* seg;

        assert(buf && len > 0);
        assert(mss_ > 0);

        // 1, 计算拆包数
        int count = (len <= mss_) ? 1 : (len + mss_ - 1) / mss_;

        // 2, 判断是否大于最大分包
        if (count >= (int)IKCP_WND_RCV) {
            // 超过最多分段时返回 -1
            return -1;
        }

        // fragment
        for (int i = 0; i < count; i++) {
            int size = len > mss_ ? mss_ : len;
            seg = (IKCPSEG *)::malloc(sizeof(IKCPSEG) + size);
            assert(seg);

            memcpy(seg->data, buf, size);
            seg->len = size;
            seg->frg = count - i - 1;
            iqueue_init(&seg->node);
            iqueue_add_tail(&seg->node, &snd_queue_);
            nsnd_que_++;
            buf += size;
            len -= size;
        }

        return 0;
    }


    /// @brief kcp update
    /// @param current 当前kcp时间(毫秒)
    void update(uint32_t now_ms) {
        int32_t slap;

        current_ = now_ms;

        if (updated_ == 0) {
            updated_ = 1;
            ts_flush_ = current_;
        }

        slap = current_ - ts_flush_;

        if (slap >= 10000 || slap < -10000) {
            ts_flush_ = current_;
            slap = 0;
        }

        if (slap >= 0) {
            if (current_ >= ts_flush_) {
                ts_flush_ = current_ + interval_;
            }
            else {
                ts_flush_ += interval_;
            }
            flush();
        }
    }


    /// @brief 将原始IO流转换为kcp数据
    /// @param data 原始IO数据
    /// @param size 原始数据长度
    /// @return 成功返回0, 否则返回!0
    int input(const uint8_t* data, size_t size) {
        uint32_t prev_una = snd_una_;
        uint32_t maxack = 0;
        int flag = 0;

        assert(data && size >= IKCP_OVERHEAD);

        while (size > 0) {
            uint32_t ts, sn, len, una, conv;
            uint16_t wnd;
            uint8_t cmd, frg;
            IKCPSEG* seg;

            if (size < IKCP_OVERHEAD) {
                return -2;
            }

            // Step 1: 解析消息头
            data = _decode32u(data, &conv);
            if (conv != conv_) {
                // 如果该消息并非来自于当前kcp
                return -1;
            }

            data = _decode8u(data, &cmd);
            data = _decode8u(data, &frg);
            data = _decode16u(data, &wnd);
             data = _decode32u(data, &ts);
            data = _decode32u(data, &sn);
            data = _decode32u(data, &una);
            data = _decode32u(data, &len);

            size -= IKCP_OVERHEAD;

            if (size < len || len > 89 * 16 /* TODO: 后期需要修改, 主要原因是为了进行[AES128]加密 */) {
                // 当原始数据长度 < 实际数据长度, 实际数据长度 大于段最大长度
                return -2;
            }

            if (cmd < IKCP_CMD_PUSH && cmd > IKCP_CMD_WINS) {
                // 无效的CMD
                return -3;
            }

            // Step 2: 设置对端窗口
            rmt_wnd_ = wnd;
            // Step 3: 将seg::sn <= una 的包移出snd_buf
            _parse_una(una);
            // Step 4: 确认kcp->snd_una, 确保该值一定是snd_buf中的最小值
            _shrink_buf();

            switch (cmd) {
            case IKCP_CMD_ACK: {
                if (current_ >= ts) {
                    // 如果当前时间 >= 分组的发送时间, 重新计算RTO
                    _update_ack(current_ - ts);
                }
                // 将ACK对应的分组移出snd_buf
                _parse_ack(sn);
                // 重新确认kcp->snd_una.
                _shrink_buf();
                if (flag == 0) {
                    flag = 1;
                    maxack = sn;
                }
                else if (sn > maxack) {
                    maxack = sn;
                }
            } break;

            case IKCP_CMD_PUSH: {
                if (sn < rcv_nxt_ + rcv_wnd_) {
                    _ack_push(sn, ts);
                    if (sn >= rcv_nxt_) {
                        seg = (IKCPSEG*)::malloc(sizeof(IKCPSEG) + len);
                        assert(seg);
                        seg->conv = conv_;
                        seg->cmd = cmd;
                        seg->frg = frg;
                        seg->wnd = wnd;
                        seg->ts = ts;
                        seg->sn = sn;
                        seg->una = una;
                        seg->len = len;

                        if (len > 0) {
                            memcpy(seg->data, data, len);
                        }

                        _parse_data(seg);
                    }
                }
            } break;

            case IKCP_CMD_WASK: {
                probe_ |= IKCP_ASK_TELL;
            } break;

            case IKCP_CMD_WINS: {
                // do nothing
            } break;

            default:
                return -3;
            }

            data += len;
            size -= len;
        }

        if (flag) {
            // 检查快速重传
            _parse_fastack(maxack);
        }

        if (snd_una_ > prev_una) {
            // 重新计算cwnd
            if (cwnd_ < rmt_wnd_) {
                uint32_t mss = mss_;
                if (cwnd_ < ssthresh_) {
                    cwnd_++;
                    incr_ += mss;
                }
                else {
                    if (incr_ < mss) {
                        incr_ = mss;
                    }
                    incr_ += (mss * mss) / incr_ + (mss / 16);
                    if ((cwnd_ + 1) * mss <= incr_) {
                        cwnd_ = (incr_ + mss - 1) / ((mss > 0) ? mss : 1);
                    }
                }

                if (cwnd_ > rmt_wnd_) {
                    cwnd_ = rmt_wnd_;
                    incr_ = rmt_wnd_ * mss;
                }
            }
        }

        return 0;
    }


    int ikcp_waitsnd()
    {
        return nsnd_buf_ + nsnd_que_;
    }


    /// @brief 将发送缓冲区的数据给 回调函数(output)处理
    void flush() {
        uint32_t now_ms = current_;
        uint8_t* buf = buffer_;
        uint8_t* ptr = buf;
        int size, i;
        uint32_t resent;
        uint32_t rtomin;
        struct IQUEUEHEAD* p;
        int change = 0;
        int lost = 0;
        IKCPSEG seg;
        ::memset(&seg, 0, sizeof(seg));
        seg.conv = conv_;
        seg.wnd = (uint32_t)_wnd_unused();
        seg.una = rcv_nxt_;

        // 'ikcp_update' haven't been called. 
        if (updated_ == 0) {
            return;
        }

        // flush acknowledges
        int count = ackcount_;
        if (count > 0) {
            seg.cmd = IKCP_CMD_ACK;

            for (i = 0; i < count; i++) {
                size = (int)(ptr - buf);
                if (size + (int)IKCP_OVERHEAD > (int)mtu_) {
                    _output(buf, size);
                    ptr = buf;
                }
                _ack_get(i, &seg.sn, &seg.ts);
                ptr = _encode_seg(ptr, &seg);
            }

            ackcount_ = 0;
        }

        // probe window size (if remote window size equals zero)
        if (rmt_wnd_ == 0) {
            if (probe_wait_ == 0) {
                probe_wait_ = IKCP_PROBE_INIT;
                ts_probe_ = now_ms + probe_wait_;
            }
            else {
                if (now_ms >= ts_probe_) {
                    if (probe_wait_ < IKCP_PROBE_INIT) {
                        probe_wait_ = IKCP_PROBE_INIT;
                    }

                    probe_wait_ += probe_wait_ / 2;
                    if (probe_wait_ > IKCP_PROBE_LIMIT) {
                        probe_wait_ = IKCP_PROBE_LIMIT;
                    }

                    ts_probe_ = now_ms + probe_wait_;
                    probe_ |= IKCP_ASK_SEND;
                }
            }
        }
        else {
            ts_probe_ = 0;
            probe_wait_ = 0;
        }

        // flush window probing commands
        if (probe_ & IKCP_ASK_SEND) {
            seg.cmd = IKCP_CMD_WASK;
            size = (int)(ptr - buf);
            if (size + (int)IKCP_OVERHEAD > (int)mtu_) {
                _output(buf, size);
                ptr = buf;
            }
            ptr = _encode_seg(ptr, &seg);
        }

        // flush window probing commands
        if (probe_ & IKCP_ASK_TELL) {
            seg.cmd = IKCP_CMD_WINS;
            size = (int)(ptr - buf);
            if (size + (int)IKCP_OVERHEAD > (int)mtu_) {
                _output(buf, size);
                ptr = buf;
            }
            ptr = _encode_seg(ptr, &seg);
        }

        probe_ = 0;

        // calculate window size
        uint32_t cwnd = _imin_(snd_wnd_, rmt_wnd_);
        if (nocwnd_ == 0) cwnd = _imin_(cwnd_, cwnd);

        // move data from snd_queue to snd_buf
        while (snd_nxt_ < snd_una_ + cwnd) {
            IKCPSEG* newseg;
            if (iqueue_is_empty(&snd_queue_)) {
                break;
            }

            newseg = iqueue_entry(snd_queue_.next, IKCPSEG, node);

            iqueue_del(&newseg->node);
            iqueue_add_tail(&newseg->node, &snd_buf_);
            nsnd_que_--;
            nsnd_buf_++;

            newseg->conv = conv_;
            newseg->cmd = IKCP_CMD_PUSH;
            newseg->wnd = seg.wnd;
            newseg->ts = now_ms;
            newseg->sn = snd_nxt_++;
            newseg->una = rcv_nxt_;
            newseg->resendts = now_ms;
            newseg->rto = rx_rto_;
            newseg->fastack = 0;
            newseg->xmit = 0;
        }

        // calculate resent
        resent = (fastresend_ > 0) ? (uint32_t)fastresend_ : 0xffffffff;
        rtomin = (nodelay_ == 0) ? (rx_rto_ >> 3) : 0;

        // flush data segments
        for (p = snd_buf_.next; p != &snd_buf_; p = p->next) {
            IKCPSEG* segment = iqueue_entry(p, IKCPSEG, node);
            int needsend = 0;
            if (segment->xmit == 0) {
                needsend = 1;
                segment->xmit++;
                segment->rto = rx_rto_;
                segment->resendts = now_ms + segment->rto + rtomin;
            }
            else if (now_ms >= segment->resendts) {
                needsend = 1;
                segment->xmit++;
                xmit_++;
                if (nodelay_ == 0) {
                    segment->rto += _imax_(segment->rto, (uint32_t)rx_rto_);
                }
                else {
                    int32_t step = (nodelay_ < 2) ? ((int32_t)(segment->rto)) : rx_rto_;
                    segment->rto += step / 2;
                }
                segment->resendts = now_ms + segment->rto;
                lost = 1;
            }
            else if (segment->fastack >= resent) {
                if ((int)segment->xmit <= fastlimit_ || fastlimit_ <= 0) {
                    needsend = 1;
                    segment->xmit++;
                    segment->fastack = 0;
                    segment->resendts = now_ms + segment->rto;
                    change++;
                }
            }

            if (needsend) {
                int need;
                segment->ts = now_ms;
                segment->wnd = seg.wnd;
                segment->una = rcv_nxt_;

                size = (int)(ptr - buf);
                need = IKCP_OVERHEAD + segment->len;

                if (size + need > (int)mtu_) {
                    _output(buf, size);
                    ptr = buf;
                }

                ptr = _encode_seg(ptr, segment);

                if (segment->len > 0) {
                    memcpy(ptr, segment->data, segment->len);
                    ptr += segment->len;
                }

                if (segment->xmit >= dead_link_) {
                    state_ = ~0;
                }
            }
        }

        // flash remain segments
        size = (int)(ptr - buf);
        if (size > 0) {
            _output(buf, size);
        }

        // update ssthresh
        if (change) {
            uint32_t inflight = snd_nxt_ - snd_una_;
            ssthresh_ = inflight / 2;
            if (ssthresh_ < IKCP_THRESH_MIN) {
                ssthresh_ = IKCP_THRESH_MIN;
            }
            cwnd_ = ssthresh_ + resent;
            incr_ = cwnd * mss_;
        }

        if (lost) {
            ssthresh_ = cwnd / 2;
            if (ssthresh_ < IKCP_THRESH_MIN) {
                ssthresh_ = IKCP_THRESH_MIN;
            }
            cwnd_ = 1;
            incr_ = mss_;
        }

        if (cwnd_ < 1) {
            cwnd_ = 1;
            incr_ = mss_;
        }
    }


    /// @brief 重置KCP
    void reset(uint32_t conv) {
        IKCPSEG* seg;
        while (!IQUEUE_IS_EMPTY(&snd_buf_)) {
            seg = IQUEUE_ENTRY(snd_buf_.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&rcv_buf_)) {
            seg = IQUEUE_ENTRY(rcv_buf_.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&snd_queue_)) {
            seg = IQUEUE_ENTRY(snd_queue_.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }
        while (!IQUEUE_IS_EMPTY(&rcv_queue_)) {
            seg = IQUEUE_ENTRY(rcv_queue_.next, IKCPSEG, node);
            IQUEUE_DEL(&seg->node);
            ::free(seg);
        }

        conv_ = conv;
        snd_una_ = 0;
        snd_nxt_ = 0;
        rcv_nxt_ = 0;
        ts_probe_ = 0;
        probe_wait_ = 0;
        cwnd_ = 0;
        incr_ = 0;
        probe_ = 0;

        nrcv_buf_ = 0;
        nsnd_buf_ = 0;
        nrcv_que_ = 0;
        nsnd_que_ = 0;
        state_ = 0;
        ackblock_ = 0;
        ackcount_ = 0;
        rx_srtt_ = 0;
        rx_rttval_ = 0;
        current_ = 0;
        nodelay_ = 0;
        updated_ = 0;
        fastresend_ = 0;
        nocwnd_ = 0;
        xmit_ = 0;
    }


    uint32_t check(uint32_t now_ms) {
        uint32_t ts_flush = ts_flush_;
        int32_t tm_flush = 0x7fffffff;
        int32_t tm_packet = 0x7fffffff;
        uint32_t minimal = 0;
        struct IQUEUEHEAD* p;

        if (updated_ == 0) {
            return 0;
        }

        int32_t slap = (int32_t)(now_ms - ts_flush);

        if (slap >= 10000 || slap < -10000) {
            ts_flush = now_ms;
        }

        if (now_ms >= ts_flush) {
            return 0;
        }

        tm_flush = -slap;

        for (p = snd_buf_.next; p != &snd_buf_; p = p->next) {
            const IKCPSEG* seg = iqueue_entry(p, const IKCPSEG, node);
            int32_t diff = (int32_t)(seg->resendts - now_ms);
            if (diff <= 0) {
                return 0;
            }
            else if (diff < tm_packet) {
                tm_packet = diff;
            }
        }

        minimal = (uint32_t)(tm_packet < tm_flush ? tm_packet : tm_flush);
        if (minimal >= interval_) {
            minimal = interval_;
        }

        return minimal;
    }


private:
    int _output(const uint8_t* data, size_t size) {
        assert(output_);
        if (size == 0) {
            return 0;
        }
        return output_(data, size, user_);
    }


    static uint8_t* _encode_seg(uint8_t* ptr, const IKCPSEG* seg) {
        ptr = _encode32u(ptr, seg->conv);
        ptr = _encode8u(ptr, (uint8_t)seg->cmd);
        ptr = _encode8u(ptr, (uint8_t)seg->frg);
        ptr = _encode16u(ptr, (uint16_t)seg->wnd);
        ptr = _encode32u(ptr, seg->ts);
        ptr = _encode32u(ptr, seg->sn);
        ptr = _encode32u(ptr, seg->una);
        ptr = _encode32u(ptr, seg->len);
        return ptr;
    }


    static uint8_t* _encode8u(uint8_t* p, uint8_t c) {
        *p++ = c;
        return p;
    }


    static const uint8_t* _decode8u(const uint8_t* p, uint8_t* c) {
        *c = *p++;
        return p;
    }


    static uint8_t* _encode16u(uint8_t* p, uint16_t w) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
        * (uint8_t*)(p + 0) = (w & 0xff);
        *(uint8_t*)(p + 1) = (w >> 8);
#else
        memcpy(p, &w, 2);
#endif
        p += 2;
        return p;
    }


    static const uint8_t* _decode16u(const uint8_t* p, uint16_t* w) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
        * w = *(const uint8_t*)(p + 1);
        *w = *(const uint8_t*)(p + 0) + (*w << 8);
#else
        memcpy(w, p, 2);
#endif
        p += 2;
        return p;
    }


    static uint8_t* _encode32u(uint8_t* p, uint32_t l) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
        * (uint8_t*)(p + 0) = (uint8_t)((l /*>>  0*/)/* & 0xff */);
        *(uint8_t*)(p + 1) = (uint8_t)((l >> 8)/* & 0xff */);
        *(uint8_t*)(p + 2) = (uint8_t)((l >> 16)/* & 0xff */);
        *(uint8_t*)(p + 3) = (uint8_t)((l >> 24)/* & 0xff */);
#else
        memcpy(p, &l, 4);
#endif
        p += 4;
        return p;
    }


    static const uint8_t* _decode32u(const uint8_t* p, uint32_t* l) {
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
        * l = *(const uint8_t*)(p + 3);
        *l = *(const uint8_t*)(p + 2) + (*l << 8);
        *l = *(const uint8_t*)(p + 1) + (*l << 8);
        *l = *(const uint8_t*)(p + 0) + (*l << 8);
#else 
        memcpy(l, p, 4);
#endif
        p += 4;
        return p;
    }


    static uint32_t _imin_(uint32_t a, uint32_t b) {
        return a <= b ? a : b;
    }

    static uint32_t _imax_(uint32_t a, uint32_t b) {
        return a >= b ? a : b;
    }

    static uint32_t _ibound_(uint32_t lower, uint32_t middle, uint32_t upper)
    {
        return _imin_(_imax_(lower, middle), upper);
    }


    void _ack_get(int p, uint32_t* sn, uint32_t* ts) {
        if (sn) sn[0] = acklist_[p * 2 + 0];
        if (ts) ts[0] = acklist_[p * 2 + 1];
    }


    int _wnd_unused() {
        return nrcv_que_ < rcv_wnd_ ? rcv_wnd_ - nrcv_que_ : 0;
    }


    int _wndsize(int sndwnd, int rcvwnd) {
        if (sndwnd > 0) {
            snd_wnd_ = sndwnd;
        }
        if (rcvwnd > 0) {   // must >= max fragment size
            rcv_wnd_ = _imax_(rcvwnd, IKCP_WND_RCV);
        }
        return 0;
    }


    void _parse_una(uint32_t una)
    {
        struct IQUEUEHEAD* p, * next;
        for (p = snd_buf_.next; p != &snd_buf_; p = next) {
            IKCPSEG* seg = iqueue_entry(p, IKCPSEG, node);
            next = p->next;
            if (una > seg->sn) {
                iqueue_del(p);
                ::free(seg);
                nsnd_buf_--;
            }
            else {
                break;
            }
        }
    }


    void _shrink_buf()
    {// 该函数确保kcp->snd_una为snd_buf中的最小值, 并且每当snd_buf队列前端的数据有变动时, 都需要调用该函数来确认snd_una
        struct IQUEUEHEAD* p = snd_buf_.next;
        if (p != &snd_buf_) {
            // 如果发送缓冲区不为空 snd_una 为发送缓冲区中最小数分组的sn
            IKCPSEG* seg = iqueue_entry(p, IKCPSEG, node);
            snd_una_ = seg->sn;
        }
        else {
            // 如果发送缓冲区为空 snd_una 为下次要发送分组的sn
            snd_una_ = snd_nxt_;
        }
    }


    void _update_ack(int32_t rtt)
    {
        int32_t rto = 0;
        if (rx_srtt_ == 0) {
            rx_srtt_ = rtt;
            rx_rttval_ = rtt / 2;
        }
        else {
            long delta = rtt - rx_srtt_;
            if (delta < 0) delta = -delta;
            rx_rttval_ = (3 * rx_rttval_ + delta) / 4;
            rx_srtt_ = (7 * rx_srtt_ + rtt) / 8;
            if (rx_srtt_ < 1) rx_srtt_ = 1;
        }
        rto = rx_srtt_ + _imax_(interval_, 4 * rx_rttval_);
        rx_rto_ = _ibound_(rx_minrto_, rto, IKCP_RTO_MAX);
    }


    void _parse_ack(uint32_t sn)
    {
        struct IQUEUEHEAD* p, *next;

        if (sn < snd_una_ || sn >= snd_nxt_) {
            return;
        }

        for (p = snd_buf_.next; p != &snd_buf_; p = next) {
            IKCPSEG* seg = iqueue_entry(p, IKCPSEG, node);
            next = p->next;
            if (sn == seg->sn) {
                iqueue_del(p);
                ::free(seg);
                nsnd_buf_--;
                break;
            }
            if (sn < seg->sn) {
                // 当前分组sn 小于缓冲区中某分组的sn时, 说明该ACK已经确认过, 并不存在于缓冲区了
                break;
            }
        }
    }


    void _ack_push(uint32_t sn, uint32_t ts)
    {
        uint32_t newsize = ackcount_ + 1;
        uint32_t* ptr;

        if (newsize > ackblock_) {
            uint32_t* tmp;
            uint32_t newblock;

            for (newblock = 8; newblock < newsize; newblock <<= 1) {}
            tmp = (uint32_t*)::malloc(newblock * sizeof(uint32_t) * 2);
            assert(tmp);

            if (acklist_ != NULL) {
                uint32_t x;
                for (x = 0; x < ackcount_; x++) {
                    tmp[x * 2 + 0] = acklist_[x * 2 + 0];
                    tmp[x * 2 + 1] = acklist_[x * 2 + 1];
                }
                ::free(acklist_);
            }

            acklist_ = tmp;
            ackblock_ = newblock;
        }

        ptr = &acklist_[ackcount_ * 2];
        ptr[0] = sn;
        ptr[1] = ts;
        ackcount_++;
    }


    void _parse_data(IKCPSEG* newseg)
    {
        struct IQUEUEHEAD* p, * prev;
        uint32_t sn = newseg->sn;
        int repeat = 0;

        if (sn >= rcv_nxt_ + rcv_wnd_ || sn < rcv_nxt_) {
            ::free(newseg);
            return;
        }

        for (p = rcv_buf_.prev; p != &rcv_buf_; p = prev) {
            IKCPSEG* seg = iqueue_entry(p, IKCPSEG, node);
            prev = p->prev;
            if (seg->sn == sn) {
                repeat = 1;
                break;
            }
            if (sn > seg->sn) {
                break;
            }
        }

        if (repeat == 0) {
            iqueue_init(&newseg->node);
            iqueue_add(&newseg->node, p);
            nrcv_buf_++;
        }
        else {
            ::free(newseg);
        }

        // move available data from rcv_buf -> rcv_queue
        while (!iqueue_is_empty(&rcv_buf_)) {
            IKCPSEG* seg = iqueue_entry(rcv_buf_.next, IKCPSEG, node);
            if (seg->sn == rcv_nxt_ && nrcv_que_ < rcv_wnd_) {
                iqueue_del(&seg->node);
                nrcv_buf_--;
                iqueue_add_tail(&seg->node, &rcv_queue_);
                nrcv_que_++;
                rcv_nxt_++;
            }
            else {
                break;
            }
        }
    }


    void _parse_fastack(uint32_t sn)
    {// 确认 缓冲区中分组 sn 被跨越了多少次
        struct IQUEUEHEAD* p, *next;

        if (sn < snd_una_ || sn >= snd_nxt_)
            return;

        for (p = snd_buf_.next; p != &snd_buf_; p = next) {
            IKCPSEG* seg = iqueue_entry(p, IKCPSEG, node);
            next = p->next;
            if (sn < seg->sn) {
                break;
            }
            else if (sn != seg->sn) {
                seg->fastack++;
            }
        }
    }


    int _peeksize()
    {
        struct IQUEUEHEAD* p;
        IKCPSEG* seg;
        int length = 0;

        if (iqueue_is_empty(&rcv_queue_)) return -1;

        seg = iqueue_entry(rcv_queue_.next, IKCPSEG, node);
        if (seg->frg == 0) return seg->len;

        if (nrcv_que_ < seg->frg + 1) return -1;

        for (p = rcv_queue_.next; p != &rcv_queue_; p = p->next) {
            seg = iqueue_entry(p, IKCPSEG, node);
            length += seg->len;
            if (seg->frg == 0) break;
        }

        return length;
    }


    uint32_t conv_, mtu_, mss_, state_;
    uint32_t snd_una_, snd_nxt_, rcv_nxt_;
    uint32_t ssthresh_;
    int32_t rx_rttval_, rx_srtt_, rx_rto_, rx_minrto_;
    uint32_t snd_wnd_, rcv_wnd_, rmt_wnd_, cwnd_, probe_;
    uint32_t current_, interval_, ts_flush_, xmit_;
    uint32_t nrcv_buf_, nsnd_buf_;
    uint32_t nrcv_que_, nsnd_que_;
    uint32_t nodelay_, updated_;
    uint32_t ts_probe_, probe_wait_;
    uint32_t dead_link_, incr_;
    struct IQUEUEHEAD snd_queue_;
    struct IQUEUEHEAD rcv_queue_;
    struct IQUEUEHEAD snd_buf_;
    struct IQUEUEHEAD rcv_buf_;
    uint32_t* acklist_;
    uint32_t ackcount_;
    uint32_t ackblock_;
    void* user_;
    uint8_t* buffer_;
    int fastresend_;
    int fastlimit_;
    int nocwnd_;
    int (*output_)(const uint8_t* buf, size_t len, void* user);


    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
}; // class Kcp


} // namespace net
} // namespace xq


#endif // !__XQ_NET_KCP__
