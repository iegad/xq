// =============================================================================================================================
// RUX: Reliable / Realtime user datagram protocol extension.
// 
// -----------------------------------------------------------------------------------------------------------------------------
// @auth: iegad
// @time: 2023-05-18
// 
// @update history
// -----------------------------------------------------------------------------------------------------------------------------
// @time                   | @note                                                                  |@coder
// 
// =============================================================================================================================



#ifndef __XQ_NET_RUX__
#define __XQ_NET_RUX__


#include "xq/net/data_type.hpp"
#include "xq/third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


// ############################################################################################################
// Rux 协议
// ############################################################################################################
class Rux {
public:
    // ========================================================================================================
    // 构造函数
    // @rid:        rux id
    // @now_us:     系统时间(微秒)
    // @output_que: io output队列
    // ========================================================================================================
    explicit Rux(uint32_t rid, uint64_t now_us, moodycamel::BlockingConcurrentQueue<PRUX_FRM>& output_que)
        : rid_(rid)
        , cwnd_(RUX_SWND_MIN)
        , ssthresh_(RUX_SSTHRESH_INIT)
        , rmt_wnd_(RUX_SWND_MIN)

        , state_(-1)
        , qid_(-1)
        , rto_(RUX_RTO_MIN)
        , srtt_(0)
        , rttval_(0)
        , nlost_(0)
        , base_us_(now_us)

        , rcv_nxt_(0)
        , snd_nxt_(0)
        , last_us_(0)
        , last_rcv_us_(0)

        , addr_({0,{0},0})
        , addrlen_(sizeof(addr_))

        , output_que_(output_que) {
        ASSERT(rid > 0 && rid <= RUX_RID_MAX);
    }


    ~Rux() {
        while (snd_buf_.size()) {
            auto itr = snd_buf_.begin();
            delete itr->second;
            snd_buf_.erase(itr);
        }

        while (rcv_buf_.size()) {
            auto itr = rcv_buf_.begin();
            delete itr->second;
            rcv_buf_.erase(itr);
        }
        
        while (ack_que_.size()) {
            auto itr = ack_que_.begin();
            delete *itr;
            ack_que_.erase(itr);
        }
    }


    inline void set_rmt_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        lkr_.lock();
        _set_remote_addr(addr, addrlen);
        lkr_.unlock();
    }


    inline int qid() const {
        return qid_;
    }


    inline void set_qid(int qid) {
        qid_ = qid;
    }


    inline uint32_t rid() const {
        return rid_;
    }


    inline int state() const {
        return state_;
    }


    inline void set_state(int state) {
        ASSERT(state == 0 || state == -1 || state == 1);
        state_ = state;
    }


    // ========================================================================================================
    // input 用于处理原始UDP数据
    // ========================================================================================================
    int input(PRUX_FRM frm) {
        int ret     = -1;
        int err     = 0;
        int datalen = frm->len - RUX_FRM_HDR_SIZE;  // Frame Payload 长度
        int n, rtt, delta;

        uint8_t* p       = frm->raw + RUX_FRM_HDR_SIZE;  // Frame Payload 数据
        uint64_t now_us  = frm->time_us - base_us_;      // 当前时间
        uint64_t last_us = 0;                            // 当前 Frame 中最大的时间(微秒)
        PRUX_SEG seg;

        std::map<uint64_t, PRUX_SEG>::iterator  itr;

        lkr_.lock();
        do {
            // update una
            while (snd_buf_.size() > 0) {
                itr = snd_buf_.begin();
                if (itr->first >= frm->una) {
                    break;
                }
                delete itr->second;
                snd_buf_.erase(itr);
            }

            while (datalen > 0) {
                seg = new RUX_SEG;

                // -------------------- Step 1: 解析 Segment --------------------
                n = seg->decode(p, datalen);
                if (n < 0) {
                    delete seg;
                    err = 1;
                    break;
                }

                if (last_us < seg->us) {
                    last_us = seg->us;
                }

                // -------------------- Step 2: 消息分发 --------------------
                switch (seg->cmd) {

                case RUX_CMD_ACK: {
                    if (seg->sn >= snd_nxt_) {
                        delete seg;
                        err = 1;
                        break;
                    }

                    itr = snd_buf_.find(seg->sn);
                    if (snd_buf_.end() != itr) {
                        delete itr->second;
                        snd_buf_.erase(itr);
                    }

                    /* ---------------------
                     * 这里可能会出现负值.
                     * 例如:
                     *    1, 发送端发 psh[sn:1, us:1] 的包.
                     *    2, 对端收到后响应 ack[sn:1, us:1], 但是该响但在网络中滞留.
                     *    3, 发送端触发重传(可能是超时重传, 也可能是快速重传) re psh[sn:1, ts:2].
                     *    4, 发送端此时收到滞留的 ack[sn:1, ts: 1], 这时就会出现RTT时间负数的情况.
                     */
                    rtt = (int)(now_us - seg->us + 1);
                    delete seg;

                    if (rtt > 0) {
                        if (srtt_ == 0) {
                            srtt_ = rtt;
                            rttval_ = rtt / 2;
                        }
                        else {
                            delta = rtt - srtt_;
                            if (delta < 0) {
                                delta = -delta;
                            }

                            rttval_ = (3 * rttval_ + delta) >> 2;
                            srtt_ = (7 * srtt_ + rtt) >> 4;
                        }

                        rto_ = MID(RUX_RTO_MIN, srtt_ + (rttval_ << 2), RUX_RTO_MAX);
                    }
                }break;

                case RUX_CMD_PIN: {
                    delete seg;
                }break;

                case RUX_CMD_PSH: {
                    if (seg->sn == 0 && snd_nxt_ == 0) {
                        delete seg;
                        err = 1;
                        break;
                    }

                    ack_que_.emplace_back(new RUX_ACK(seg->sn, seg->us));

                    if (seg->sn >= rcv_nxt_ && rcv_buf_.count(seg->sn) == 0) {
                        seg->rid = frm->rid;
                        seg->time_us = frm->time_us;
                        seg->addr = &frm->name;
                        seg->addrlen = frm->namelen;
                        rcv_buf_.insert(std::make_pair(seg->sn, seg));
                        if (rcv_buf_.size() > RUX_SWND_MAX) {
                            DLOG("###################################################################### %lu\n", rcv_buf_.size());
                        }
                    }
                    else {
                        delete seg;
                    }
                }break;

                case RUX_CMD_CON: {
                    if (seg->sn != 0) {
                        delete seg;
                        err = 1;
                        break;
                    }

                    ack_que_.emplace_back(new RUX_ACK(seg->sn, seg->us));

                    if (rcv_buf_.count(seg->sn) == 0) {
                        seg->rid = frm->rid;
                        seg->time_us = frm->time_us;
                        seg->addr = &frm->name;
                        seg->addrlen = frm->namelen;
                        _reset(seg->time_us);
                        rcv_buf_.insert(std::make_pair(seg->sn, seg));
                    }
                    else {
                        delete seg;
                    }
                }break;

                default: {
                    delete seg;
                    err = 1;
                }break;
                } // switch

                if (err) {
                    break;
                }

                p += n;
                datalen -= n;
            } // while (datalen > 0);

            if (err) {
                break;
            }

            // Step 3: 设置新的 remote window
            if (last_us_ < last_us || (last_us == 0 && frm->len == RUX_FRM_HDR_SIZE)) {
                last_us_ = last_us;
                rmt_wnd_ = frm->wnd;
            }

            // Step 4: 拥塞控制
            if (rmt_wnd_ > cwnd_) {
                if (cwnd_ < ssthresh_) {
                    cwnd_ <<= 1;
                }
                else if (cwnd_ < RUX_SWND_MAX) {
                    cwnd_++;
                }
            }
            else if (rmt_wnd_ == 0) {
                ssthresh_ = cwnd_ / 2;
                rmt_wnd_ = cwnd_ = 1;
            }

            last_rcv_us_ = frm->time_us;
            _set_remote_addr(&frm->name, frm->namelen);
            ret = 0;
        } while (0);
        lkr_.unlock();

        return ret;
    }


    // ========================================================================================================
    // recv 从读缓冲区获取rux message
    // @return 读缓冲区中有数据返回 msg数据长度, 否则返回 0
    // ========================================================================================================
    int recv(uint8_t* msg) {
        int n = 0, ok = 0, i;
        uint16_t len;
        PRUX_SEG segs[RUX_FRM_MAX];
        PRUX_SEG seg;
        std::map<uint64_t, PRUX_SEG>::iterator itr;
        uint8_t* p = msg;

        lkr_.lock();
        uint64_t nxt = rcv_nxt_;
        itr = rcv_buf_.begin();

        while (itr != rcv_buf_.end()) {
            seg = itr->second;

            if (seg->sn != nxt) {
                break;
            }

            nxt++;
            segs[n++] = seg;
            if (!seg->frg) {
                ok = 1;
                break;
            }
        }

        if (ok) {
            for (i = 0; i < n; i++) {
                seg = segs[i];
                len = seg->len;
                if (len) {
                    ::memcpy(p, seg->data, len);
                    p += len;
                }
                delete seg;
            }

            rcv_buf_.erase(rcv_buf_.begin(), ++itr);
            rcv_nxt_ = nxt;
        }

        lkr_.unlock();
        return (int)(p - msg);
    }


    // ========================================================================================================
    // send 将msg 转换为 segment 并投递发送缓冲区
    // ========================================================================================================
    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        int ret = -1;

        lkr_.lock();
        do {
            if (state_ == -1) {
                break;
            }

            PRUX_SEG seg;

            for (int i = 1, n = msglen > RUX_MSS ? (msglen + RUX_MSS - 1) / RUX_MSS : 1; i <= n; i++) {
                int len = i == n ? msglen : RUX_MSS;
                seg = new RUX_SEG;
                if (state_ == 0) {
                    seg->cmd = RUX_CMD_CON;
                    state_ = 1;
                }
                else {
                    seg->cmd = RUX_CMD_PSH;
                }

                seg->sn = snd_nxt_++;
                seg->frg = n - i;
                seg->set_data(msg, len);

                snd_buf_.insert(std::make_pair(seg->sn, seg));

                msg += len;
                msglen -= len;
            }

            ret = 0;
        } while (0);
        lkr_.unlock();

        return ret;
    }


    // ========================================================================================================
    // output 将需要发送的协议数据投递到 output_que中
    // ========================================================================================================
    int output(uint64_t now_us) {
        constexpr int LOST_LIMIT = 50;
        constexpr uint64_t LOST_INTERVAL = 1000 * 1000 * 10;

        int already_snd = 0;
        int err         = 0;
        int needsnd     = 0;
        int nfrms       = 0;
        int nleft       = 0;
        int ret         = -1;
        int i, n;

        uint8_t* p      = nullptr;
        PRUX_FRM frm    = nullptr;
        PRUX_ACK ack;
        PRUX_SEG pseg;
        
        RUX_SEG  seg;
        PRUX_FRM frms[RUX_RWND_MAX * 2];

        std::list<PRUX_ACK>::iterator          ack_itr;
        std::map<uint64_t, PRUX_SEG>::iterator psh_itr;

        lkr_.lock();
        do {
            if (now_us > last_rcv_us_) {
                uint64_t diff_us = now_us - last_rcv_us_;
                if (last_rcv_us_ > 0) {
                    if (diff_us > RUX_TIMEOUT) {
                        state_ = -1;
                    }
                    else if (diff_us >= LOST_INTERVAL) {
                        if (nlost_ > LOST_LIMIT) {
                            DLOG(" state changed, rux closed\n");
                            state_ = -1;
                        }
                        else {
                            nlost_ = 0;
                        }
                    }
                }
            }

            if (state_ == -1) {
                break;
            }

            now_us -= base_us_;

            // ACK
            while (ack_que_.size() > 0) {
                ack_itr = ack_que_.begin();
                if (frm && nleft < RUX_SEG_HDR_SIZE) {
                    frm->len = (uint16_t)(p - frm->raw);
                    ASSERT(!frm->setup());
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    ASSERT(frm);
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                if (!frm) {
                    frm = _new_frm();
                    ASSERT(frm);
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                ack = *ack_itr;
                if (ack->sn >= frm->una) {
                    seg.cmd = RUX_CMD_ACK;
                    seg.sn = ack->sn;
                    seg.us = ack->us;
                    n = seg.encode(p, nleft);
                    ASSERT(n == 13);
                    p += n;
                    nleft -= n;
                }

                delete ack;
                ack_que_.erase(ack_itr++);
            }

            // PSH
            int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
            psh_itr = snd_buf_.begin();
            while (already_snd < snd_wnd && psh_itr != snd_buf_.end()) {
                pseg = psh_itr->second;

                if (frm && nleft < RUX_SEG_HDR_EX_SIZE + pseg->len) {
                    frm->len = (uint16_t)(p - frm->raw);
                    ASSERT(!frm->setup());
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    ASSERT(frm);
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                if (pseg->xmit == 0) {
                    pseg->rto = rto_;
                    needsnd = 1;
                }
                else if (pseg->xmit >= RUX_XMIT_MAX) {
                    DLOG(" state changed, rux xmit limit\n");
                    state_ = -1;
                    for (i = 0; i < nfrms; i++) {
                        delete frms[i];
                    }

                    if (frm) {
                        delete frm;
                    }

                    err = 1;
                    break;
                }
                else {
                    if (pseg->resend_us <= now_us) {
                        DLOG("-------------------------------------------------------\n");
                        pseg->rto += rto_ + rto_ / 2;
                        ssthresh_ = cwnd_ / 2;
                        cwnd_ = 1;
                        snd_wnd = 1;
                        needsnd = 1;
                    }
                    else if (pseg->fastack >= RUX_FAST_ACK) {
                        DLOG("########################################################\n");
                        pseg->fastack = 0;
                        pseg->rto = rto_;

                        cwnd_ /= 2;
                        ssthresh_ = cwnd_;
                        needsnd = 1;
                    }
                    else if (pseg->sn == 0) {
                        break;
                    }
                }

                if (needsnd) {
                    pseg->xmit++;
                    pseg->us = now_us;
                    pseg->resend_us = pseg->rto + now_us;
                    if (pseg->xmit > 1) {
                        nlost_++;
                    }

                    if (!frm) {
                        frm = _new_frm();
                        ASSERT(frm);
                        p = frm->raw + RUX_FRM_HDR_SIZE;
                        nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                    }

                    n = pseg->encode(p, nleft);
                    ASSERT(n > 0);
                    p += n;
                    nleft -= n;
                    already_snd++;
                    needsnd = 0;
                }

                psh_itr++;
            } // while (already_snd < snd_wnd && psh_itr != snd_buf_.end());

            if (err) {
                break;
            }

            ret = 0;
        } while (0);

        lkr_.unlock();

        if (ret != -1 && frm) {
            frm->len = (uint16_t)(p - frm->raw);
            ASSERT(!frm->setup());
            frms[nfrms++] = frm;

            if (nfrms > 0) {
                ASSERT(output_que_.enqueue_bulk(frms, nfrms));
            }
        }

        return ret;
    }


private:
    void _reset(uint64_t now_us) {
        while (ack_que_.size() > 0) {
            auto itr = ack_que_.begin();
            delete *itr;
            ack_que_.erase(itr);
        }

        rcv_buf_.clear();

        while (snd_buf_.size() > 0) {
            auto itr = snd_buf_.begin();
            delete itr->second;
            snd_buf_.erase(itr);
        }

        nlost_ = rttval_ = srtt_ = 0;
        rmt_wnd_ = cwnd_ = RUX_SWND_MIN;
        rto_ = RUX_RTO_MIN;

        last_rcv_us_ = rcv_nxt_ = snd_nxt_ = last_us_ = 0;
        base_us_ = now_us;
    }

    inline void _set_remote_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
        }

        if (::memcmp(&addr_, addr, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
        }
    }


    inline PRUX_FRM _new_frm() {
        PRUX_FRM frm = new RUX_FRM;
        frm->rid = rid_;
        frm->wnd = (uint8_t)(rcv_buf_.size() > RUX_RWND_MAX ? 0 : RUX_RWND_MAX - rcv_buf_.size());
        frm->una = rcv_nxt_;
        ::memcpy(&frm->name, &addr_, addrlen_);
        frm->namelen = addrlen_;
        return frm;
    }


    uint32_t                                        rid_;                       // rux id

    uint8_t                                         cwnd_;                      // 拥塞控制
    uint8_t                                         ssthresh_;
    uint8_t                                         rmt_wnd_;                   // 对端窗口

    std::atomic<int>                                state_;
    std::atomic<int>                                qid_;                       // rux_que index
    int                                             rto_;                       // RTO
    int                                             srtt_;                      // smooth RTT
    int                                             rttval_;                    
    int                                             nlost_;
    uint64_t                                        base_us_;                   // 启始时间(微秒)
    
    uint64_t                                        rcv_nxt_;                   // 下一次接收 sn
    uint64_t                                        snd_nxt_;                   // 下一次发送 sn
    uint64_t                                        last_us_;                   // 上一次sn
    uint64_t                                        last_rcv_us_;               // 最后一次接收时间

    sockaddr_storage                                addr_;                      // 对端地址
    socklen_t                                       addrlen_;                   // 对端地址长度

    std::list<PRUX_ACK>                             ack_que_;                   // ACK队列
    std::map<uint64_t, PRUX_SEG>                    snd_buf_;                   // 发送缓冲区
    std::map<uint64_t, PRUX_SEG>                    rcv_buf_;                   // 接收缓冲区

    SPIN_LOCK                                       lkr_;
    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   &output_que_;               // io output queue 引用
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
