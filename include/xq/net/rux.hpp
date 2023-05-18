/* ---------------------------------------------------------------------------------------------------------------------------------------
 * RUX: Reliable / Realtime user datagram protocol extension.
 * 
 * -----------------------------------------------------------------------------------------------------------------------------
 * @auth: iegad
 * @time: 2023-05-18
 * 
 * @update history
 * -----------------------------------------------------------------------------------------------------------------------------
 * @time                   | @note                                                                  |@coder
 * 
 * --------------------------------------------------------------------------------------------------------------------------------------- */



#ifndef __XQ_NET_RUX__
#define __XQ_NET_RUX__


#include "xq/net/common.hpp"
#include "xq/third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


// ----------------------------------------
/// @brief Rux 协议, 不涉及IO
///
class Rux {
public:
    // ---------------------------------------------
    // 构造函数
    // @rid:        rux id
    // @now_us:     系统时间(微秒)
    // @output_que: io output队列
    // ---------------------------------------------
    explicit Rux(uint32_t rid, uint64_t now_us, moodycamel::BlockingConcurrentQueue<PRUX_FRM>& output_que)
        : qid_(-1)
        , state_(0)
        , rid_(rid)
        , cwnd_(RUX_SWND_MIN)
        , rmt_wnd_(RUX_SWND_MIN)
        , probe_(0)
        , rto_(RUX_RTO_MIN)
        , srtt_(0)
        , rttval_(0)
        , base_us_(now_us)

        , rcv_nxt_(0)
        , snd_nxt_(0)
        , snd_una_(0)
        , last_sn_(0)

        , addr_({{0},0,{0}})
        , addrlen_(sizeof(addr_))

        , nabuf_(0)
        , nsbuf_(0)
        , nrbuf_(0)
        , output_que_(output_que) {
        ::memset(abuf_, 0, sizeof(abuf_));
        ::memset(sbuf_, 0, sizeof(sbuf_));
        ::memset(rbuf_, 0, sizeof(rbuf_));
    }


    // ---------------------------------------------
    // 获取 que_id
    //    每个rux都有一个工作队列的编号(qid)
    // ---------------------------------------------
    int get_qid() const {
        return qid_;
    }


    // ---------------------------------------------
    // 设置 que_id
    //    RuxServer 在rux收到RUX_CMD_CON时, 
    //    会为rux以RoundRobin方式分配一个que, 并设置该rux的que_id
    // ---------------------------------------------
    void set_qid(int qid) {
        qid_ = qid;
    }


    // ---------------------------------------------
    // rux remote 地址
    // ---------------------------------------------
    sockaddr_storage* addr() {
        return &addr_;
    }

    // ---------------------------------------------
    // rux remote 地址长度
    // ---------------------------------------------
    socklen_t* addrlen() {
        return &addrlen_;
    }


    // ---------------------------------------------
    // input 用于处理原始UDP数据
    // ---------------------------------------------
    int input(PRUX_FRM frm) {
        int         datalen     = frm->len - RUX_FRM_HDR_SIZE;  // 获取Frame Payload 长度
        int         n;
        int         ret;
        uint8_t*    p           = frm->raw + RUX_FRM_HDR_SIZE;  // 获取Frame Payload 数据
        PRUX_SEG    seg;
        uint64_t    last_sn     = 0;
        uint8_t     rmt_wnd     = 0;

        while (datalen > 0) {
            seg = new RUX_SEG;
            // Step 1: 解析 RuxSegment
            n = seg->decode(p, datalen);
            if (n < 0) {
                delete seg;
                return -1;
            }

            // Step 2: 确定seg->sn 的正确范围
            if (seg->sn > rcv_nxt_ + RUX_RWND_MAX) {
                delete seg;
                return -1;
            }

            // Step 3: 消息分发
            switch (seg->cmd) {

            case RUX_CMD_ACK: {
                seg->time_us = frm->time_us;
                ret = _ack_handle(seg);
            }break;

            case RUX_CMD_PIN: {
                ret = _pin_handle(seg);
            }break;

            case RUX_CMD_PON: {
                ret = _pon_handle(seg);
            }break;

            case RUX_CMD_PSH: {
                seg->rid = frm->rid;
                seg->time_us = frm->time_us;
                seg->addr = &frm->name;
                seg->addrlen = frm->namelen;
                ret = _psh_handle(seg);
            }break;

            case RUX_CMD_CON: {
                seg->rid = frm->rid;
                seg->time_us = frm->time_us;
                seg->addr = &frm->name;
                seg->addrlen = frm->namelen;
                ret = _con_handle(seg);
            }break;

            default: delete seg; return -1;
            } // switch

            p += n;
            datalen -= n;

            if (last_sn < seg->sn) {
                last_sn = seg->sn;
            }
        }

        if (last_sn_ < last_sn) {
            last_sn_ = last_sn;
            rmt_wnd_ = frm->wnd;
        }

        // Step 4: 拥塞控制
        // TODO: Congesion Controller

        // Step 5: 设置对端地址
        _set_remote_addr(&frm->name, frm->namelen);
        return 0;
    }


    // ---------------------------------------------
    // recv 从读缓冲区获取rux message
    // @return 读缓冲区中有数据返回 msg数据长度, 否则返回 0
    // ---------------------------------------------
    int recv(uint8_t* msg, int n) {
        ASSERT(msg && n > 0);
        PRUX_SEG seg, segs[RUX_FRM_MAX];
        int nsegs = 0, ok = 0, i;
        uint64_t sn, max_sn;;

        for (sn = rcv_nxt_, max_sn = rcv_nxt_ + nrbuf_; sn < max_sn; sn++) {
            i = sn % sizeof(rbuf_);
            seg = rbuf_[i];
            if (!seg || seg->sn != sn) {
                return 0;
            }

            segs[nsegs++] = seg;
            if (seg->frg == 0) {
                ok = 1;
                break;
            }
        }

        if (ok) {
            rcv_nxt_ = sn + 1;
            uint8_t* p = msg;
            for (i = 0; i < nsegs; i++) {
                seg = segs[i];
                ::memcpy(p, seg->data, seg->len);
                p += seg->len;
                rbuf_[seg->sn % sizeof(rbuf_)] = nullptr;
                delete seg;
            }

            nrbuf_ -= nsegs;
            return p - msg;
        }

        return 0;
    }


    // ---------------------------------------------
    // send 将msg 转换为 segment 并投递发送缓冲区
    // ---------------------------------------------
    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        int n = msglen > RUX_MSS ? (msglen + RUX_MSS - 1) / RUX_MSS : 1;
        PRUX_SEG seg;

        for (int i = 1; i <= n; i++) {
            uint64_t len = i == n ? msglen : RUX_MSS;
            seg = new RUX_SEG;
            seg->cmd = snd_nxt_ == 0 && rcv_nxt_ == 0 && nsbuf_ == 0 ? RUX_CMD_CON : RUX_CMD_PSH;
            seg->sn = snd_nxt_++;
            seg->frg = n - i;
            seg->set_data(msg, len);

            sbuf_[seg->sn % sizeof(sbuf_)] = seg;
            msg += len;
            msglen -= len;
        }
        
        nsbuf_ += n;
        return 0;
    }


    // ---------------------------------------------
    // flush 将需要发送的协议数据投递到 output_que中
    // ---------------------------------------------
    int flush(uint64_t now_us) {
        now_us -= base_us_;

        PRUX_FRM frm = _new_frm();

        uint8_t* p = frm->raw + RUX_FRM_HDR_SIZE;
        uint16_t nleft = RUX_MTU - RUX_FRM_HDR_SIZE, n;
        RUX_SEG seg, *pseg;

        PRUX_FRM frms[128];
        int nfrms = 0;

        // ACK
        if (nabuf_ > 0) {
            for (int i = 0; i < nabuf_; i++) {
                if (nleft < RUX_SEG_HDR_SIZE) {
                    frm->len = p - frm->raw;
                    frm->len = p - frm->raw;
                    frm->rid = rid_;
                    frm->wnd = RUX_RWND_MAX - nrbuf_;
                    frm->setup();
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                PRUX_ACK ack = abuf_[i];
                seg.cmd = RUX_CMD_ACK;
                seg.sn = ack->sn;
                seg.us = ack->us;
                n = seg.encode(p, nleft);
                p += n;
                nleft -= n;
            }

            nabuf_ = 0;
        }

        // snd_buffer
        uint8_t snd_wnd = MIN(rmt_wnd_, cwnd_);
        if (nsbuf_ < (uint64_t)snd_wnd) {
            snd_wnd = (uint8_t)nsbuf_;
        }

        if (snd_wnd > 0) {
            int needsend = 0;

            for (uint64_t sn = snd_una_, max_sn = snd_wnd + snd_una_; sn < max_sn; sn++) {
                pseg = sbuf_[sn % sizeof(sbuf_)];
                if (!pseg) {
                    continue;
                }

                if (nleft < RUX_SEG_HDR_EX_SIZE + pseg->len) {
                    frm->len = p - frm->raw;
                    frm->setup();
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                if (pseg->xmit > RUX_XMIT_MAX) {
                    state_ = 0;
                    break;
                }

                if (pseg->xmit == 0) {
                    pseg->rto = rto_;
                    needsend = 1;
                }
                else if (pseg->resend_us <= now_us) {
                    pseg->rto *= 1.3;
                    needsend = 1;
                }
                else if (pseg->fastack >= RUX_FAST_ACK) {
                    pseg->fastack = 0;
                    pseg->rto = rto_;
                }

                if (needsend) {
                    pseg->resend_us = pseg->rto + now_us;
                    pseg->us = now_us;
                    pseg->xmit++;

                    n = pseg->encode(p, nleft);
                    p += n;
                    nleft -= n;

                    needsend = 0;
                }
            }
        }

        if (!state_) {
            return -1;
        }

        if (nleft == RUX_MTU - RUX_FRM_HDR_SIZE) {
            delete frm;
        }
        else {
            frm->len = p - frm->raw;
            frm->setup();
            frms[nfrms++] = frm;
        }

        ASSERT(output_que_.enqueue_bulk(frms, nfrms));
        return 0;
    }


    // ---------------------------------------------
    // active 激活当前rux, 并重置当前rux
    // ---------------------------------------------
    void active(uint64_t now_us) {
        for (int i = 0; i < nabuf_; i++) {
            delete abuf_[i];
        }
        nabuf_ = 0;

        for (uint64_t sn = snd_una_, max_sn = nsbuf_ + snd_una_; sn < max_sn; sn++) {
            delete sbuf_[sn % sizeof(sbuf_)];
        }
        nsbuf_ = 0;

        state_ = 1;
        rmt_wnd_ = rttval_ = srtt_ = probe_ = 0;
        rmt_wnd_ = cwnd_ = RUX_SWND_MIN;
        rto_ = RUX_RTO_MIN;

        rcv_nxt_ = snd_nxt_ = snd_una_ = last_sn_ = 0;
    }


private:
    PRUX_FRM _new_frm() {
        PRUX_FRM frm = new RUX_FRM;
        frm->rid = rid_;
        frm->wnd = RUX_RWND_MAX - nrbuf_;
        ::memcpy(&frm->name, &addr_, addrlen_);
        frm->namelen = addrlen_;
        return frm;
    }


    void _set_remote_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
        }

        if (memcmp(addr, &addr_, addrlen)) {
            memcpy(&addr_, addr, addrlen);
        }
    }


    int _ack_handle(PRUX_SEG seg) {
        int ret = -1;

        do {
            // Step 1: 检查seg->sn, ACK sn 必需在发送窗口范围内
            if (seg->sn > snd_una_ + nsbuf_) {
                break;
            }

            // Step 2: 从send buffer 中移除对应的 Segment
            InterlockedExchangePointer((void**)&(sbuf_[seg->sn % sizeof(sbuf_)]), nullptr);
            InterlockedDecrement(&nsbuf_);

            // Step 3: 计算 Smooth RTT
            int64_t rtt = seg->time_us - base_us_ - seg->us;
            if (rtt < 0) {
                ret = 0;
                break;
            }
            else if (rtt > RUX_RTO_TIMEOUT) {
                break;
            }

            if (srtt_ == 0) {
                srtt_ = rtt;
                rttval_ = rtt / 2;
            }
            else {
                int64_t d = rtt - srtt_;
                if (d < 0) {
                    d = -d;
                }

                rttval_ = (int)((3 * rttval_ + d) / 4);
                srtt_ = (int)((7 * srtt_ + rtt) / 8);
            }

            if (srtt_ < 1) {
                srtt_ = 1;
            }

            // Step 4: 计算RTO
            rto_ = MID(RUX_RTO_MIN, srtt_ + 4 * rttval_, RUX_RTO_MAX);
            ret = 0;
        } while (0);

        delete seg;
        return ret;
    }


    int _pin_handle(PRUX_SEG seg) {
        probe_ = 1;
        if (seg->sn == rcv_nxt_) {
            rcv_nxt_++;
        }
        delete seg;
        return 0;
    }


    int _pon_handle(PRUX_SEG seg) {
        if (seg->sn == rcv_nxt_) {
            rcv_nxt_++;
        }
        delete seg;
        return 0;
    }


    int _con_handle(PRUX_SEG seg) {
        if (seg->sn != 0) {
            delete seg;
            return -1;
        }

        active(seg->time_us);
        
        int ai = seg->sn % sizeof(abuf_);
        int ri = seg->sn % sizeof(rbuf_);
        if (!abuf_[ai]) {
            abuf_[ai] = new RUX_ACK(seg->sn, seg->us);
            InterlockedIncrement(&nabuf_);
        }

        if (!rbuf_[ri]) {
            rbuf_[ri] = seg;
            nrbuf_++;
        }

        return 0;
    }


    int _psh_handle(PRUX_SEG seg) {
        if (seg->sn == 0) {
            delete seg;
            return -1;
        }
        
        int ai = seg->sn % sizeof(abuf_);
        int ri = seg->sn % sizeof(rbuf_);
        if (!abuf_[ai]) {
            abuf_[ai] = new RUX_ACK(seg->sn, seg->us);
            InterlockedIncrement(&nabuf_);
        }
        
        if (!rbuf_[ri]) {
            rbuf_[ri] = seg;
            nrbuf_++;
        }

        return 0;
    }


    int                                             qid_;                       // 工作队列id
    int                                             state_;                     // 状态
    uint32_t                                        rid_;                       // id
    uint8_t                                         cwnd_;                      // 拥塞控制
    uint8_t                                         rmt_wnd_;                   // 对端窗口
    uint8_t                                         probe_;                     // 探针
    int                                             rto_;                       // RTO
    int                                             srtt_;                      // smooth rtt
    int                                             rttval_;                    
    uint64_t                                        base_us_;                   // 启始时间(微秒)
    
    uint64_t                                        rcv_nxt_;                   // 下一次接收 sn
    uint64_t                                        snd_nxt_;                   // 下一次发送 sn
    uint64_t                                        snd_una_;                   // 发送窗口
    uint64_t                                        last_sn_;                   // 上一次sn

    sockaddr_storage                                addr_;                      // 对端地址
    socklen_t                                       addrlen_;                   // 对端地址长度

    long                                            nabuf_;                     // ACK队列长度
    PRUX_ACK                                        abuf_[RUX_SWND_MAX * 4];    // ACK队列

    long                                            nsbuf_;                     // 发送队列长度
    PRUX_SEG                                        sbuf_[RUX_SWND_MAX * 4];    // 发送队列

    long                                            nrbuf_;                     // 接收队列长度
    PRUX_SEG                                        rbuf_[RUX_RWND_MAX];        // 接收队列

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   &output_que_;               // io output queue 引用
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
