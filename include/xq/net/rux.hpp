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


#include "xq/net/common.hpp"
#include "xq/third/blockingconcurrentqueue.h"


namespace xq {
namespace net {


// ============================================================================================================
/// @brief Rux 协议, 不涉及IO
///
class Rux {
public:
    // ========================================================================================================
    // 构造函数
    // @rid:        rux id
    // @now_us:     系统时间(微秒)
    // @output_que: io output队列
    // ========================================================================================================
    explicit Rux(uint32_t rid, uint64_t now_us, moodycamel::BlockingConcurrentQueue<PRUX_FRM>& output_que)
        : state_(-1)
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
        , last_us_(0)
        , snd_us_(-1)

        , addr_({0,{0},0})
        , addrlen_(sizeof(addr_))

        , output_que_(output_que) {
        ASSERT(rid > 0 && rid <= RUX_RID_MAX);
    }


    // ========================================================================================================
    // rux remote 地址
    // ========================================================================================================
    sockaddr_storage* addr() {
        return &addr_;
    }


    // ========================================================================================================
    // rux remote 地址长度
    // ========================================================================================================
    socklen_t* addrlen() {
        return &addrlen_;
    }


    // ========================================================================================================
    // input 用于处理原始UDP数据
    // ========================================================================================================
    int input(PRUX_FRM frm) {
        int         datalen     = frm->len - RUX_FRM_HDR_SIZE;  // 获取Frame Payload 长度
        int         n;
        uint8_t*    p           = frm->raw + RUX_FRM_HDR_SIZE;  // 获取Frame Payload 数据
        uint64_t    last_us     = 0;                            // 当前 Frame 中最大的时间(微秒)
        uint64_t    now_us      = frm->time_us - base_us_;      // 当前时间
        PRUX_SEG    seg;

        while (datalen > 0) {
            seg = new RUX_SEG;

            // -------------------- Step 1: 解析 Segment --------------------
            n = seg->decode(p, datalen);
            if (n < 0) {
                delete seg;
                return -1;
            }

            if (last_us < seg->us) {
                last_us = seg->us;
            }

            // -------------------- Step 2: 消息分发 --------------------
            DLOG("[INPUT] seg SN[%llu] CMD[%d]\n", seg->sn, seg->cmd);
            switch (seg->cmd) {

            /* ------------- RUX_CMD_ACK -------------
             * 1, 确认seg->sn的范围;
             * 2, 从snd_buf中移除相应的seg;
             * 3, 计算rtt, srtt, rto;
             * 
             * RTO的计算采用 RFC-2988 的计算方式;
             */
            case RUX_CMD_ACK: {
                if (seg->sn >= snd_nxt_) {
                    delete seg;
                    return -1;
                }

                snd_buf_.erase(seg->sn);

                /* ---------------------
                 * 这里可能会出现负值.
                 * 例如:
                 *    1, 发送端发 psh[sn:1, us:1] 的包.
                 *    2, 对端收到后响应 ack[sn:1, us:1], 但是该响但在网络中滞留.
                 *    3, 发送端触发重传(可能是超时重传, 也可能是快速重传) re psh[sn:1, ts:2].
                 *    4, 发送端此时收到滞留的 ack[sn:1, ts: 1], 这时就会出现RTT时间负数的情况.
                 */
                int rtt = (int)(now_us - seg->us + 1);
                if (rtt > 0) {

                    if (srtt_ == 0) {
                        srtt_ = rtt;
                        rttval_ = rtt / 2;
                    }
                    else {
                        int d = rtt - srtt_;
                        if (d < 0) {
                            d = -d;
                        }

                        rttval_ = (3 * rttval_ + d) / 4;
                        srtt_ = (7 * srtt_ + rtt) / 8;
                    }

                    rto_ = MID(RUX_RTO_MIN, srtt_ + 4 * rttval_, RUX_RTO_MAX);
                }
                else if (rtt >= RUX_TIMEOUT) {
                    state_ = -1;
                    return -1;
                }

                delete seg;
            }break;

            /* ------------- RUX_CMD_PIN -------------
             * 1, 将探针标志置为 1;
             * 2, 将snd_us_ 置0;
             * 
             * 下一次 update 时发送 RUX_CMD_PON segment
             */
            case RUX_CMD_PIN: {
                probe_ = 1;
                snd_us_ = 0;
            }break;

            /* ------------- RUX_CMD_PON -------------
             * nothing todo
             */
            case RUX_CMD_PON: break;

            /* ------------- RUX_CMD_PSH -------------
             * 1, 检查seg->sn范围;
             * 2, 检查该包是否处理过;
             */
            case RUX_CMD_PSH: {
                if (seg->sn == 0 || seg->sn >= RUX_RWND_MAX + rcv_nxt_) {
                    delete seg;
                    return -1;
                }

                if (seg->sn < rcv_nxt_) {
                    delete seg;
                }
                else {
                    if (rcv_buf_.count(seg->sn) == 0) {
                        seg->rid = frm->rid;
                        seg->time_us = frm->time_us;
                        seg->addr = &frm->name;
                        seg->addrlen = frm->namelen;
                        rcv_buf_.insert(std::make_pair(seg->sn, seg));
                        if (rcv_buf_.size() > RUX_SWND_MAX) {
                            DLOG("###################################################################### %llu\n", rcv_buf_.size());
                        }
                    }

                    ack_que_.insert(seg->sn, seg->us);
                    snd_us_ = 0;
                }
            }break;

            /* ------------- RUX_CMD_CON -------------
             * 1, 检查seg->sn 是否为第一个包(seg->sn == 0);
             * 2, 设置seg的META属性;
             * 3, 激活当前rux;
             * 4, 生成ACK并加入ack_que_中;
             * 5, 将psh segment加入rcv_buf_中;
             */
            case RUX_CMD_CON: {
                if (seg->sn != 0) {
                    delete seg;
                    return -1;
                }
                
                if (rcv_buf_.count(seg->sn) == 0) {
                    seg->rid = frm->rid;
                    seg->time_us = frm->time_us;
                    seg->addr = &frm->name;
                    seg->addrlen = frm->namelen;
                    active(seg->time_us);
                    rcv_buf_.insert(std::make_pair(seg->sn, seg));
                }

                ack_que_.insert(seg->sn, seg->us);
                snd_us_ = 0;
            }break;

            default: delete seg; break;
            } // switch

            p += n;
            datalen -= n;
        }

        // Step 3: 设置新的 remote window
        if (last_us_ <= last_us) {
            last_us_ = last_us;
            rmt_wnd_ = frm->wnd;
            if (rmt_wnd_ == 0) {
                snd_us_ = 0;
            }
        }

        // Step 4: 拥塞控制
        // TODO: Congesion Controller

        // Step 5: 设置对端地址
        _set_remote_addr(&frm->name, frm->namelen);
        return 0;
    }


    // ========================================================================================================
    // recv 从读缓冲区获取rux message
    // @return 读缓冲区中有数据返回 msg数据长度, 否则返回 0
    // ========================================================================================================
    int recv(uint8_t* msg, int n) {
        int i, ok = 0, nsegs = 0;

        uint64_t                                nxt = rcv_nxt_;
        PRUX_SEG                                seg;
        PRUX_SEG                                segs[RUX_FRM_MAX];
        std::map<uint64_t, PRUX_SEG>::iterator  itr;

        for (itr = rcv_buf_.begin(); itr != rcv_buf_.end(); ++itr) {
            seg = itr->second;
            if (seg->sn != nxt) {
                return 0;
            }

            nxt++;
            segs[nsegs++] = seg;
            if (!seg->frg) {
                ok = 1;
                itr++;
                break;
            }
        }

        if (ok) {
            rcv_nxt_ = nxt;
            rcv_buf_.erase(rcv_buf_.begin(), itr);

            uint8_t* p = msg;
            for (i = 0; i < nsegs; i++) {
                seg = segs[i];
                ::memcpy(p, seg->data, seg->len);
                p += seg->len;
                delete seg;
            }

            return p - msg;
        }

        return 0;
    }


    // ========================================================================================================
    // send 将msg 转换为 segment 并投递发送缓冲区
    // ========================================================================================================
    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        if (snd_buf_.size() >= RUX_SWND_MAX) {
            return -1;
        }

        PRUX_SEG seg;

        for (int i = 1, n = msglen > RUX_MSS ? (msglen + RUX_MSS - 1) / RUX_MSS : 1; i <= n; i++) {
            uint64_t len = i == n ? msglen : RUX_MSS;
            seg = new RUX_SEG;
            seg->cmd = snd_nxt_ == 0 && rcv_nxt_ == 0 && snd_buf_.size() == 0 ? RUX_CMD_CON : RUX_CMD_PSH;
            seg->sn = snd_nxt_++;
            seg->frg = n - i;
            seg->set_data(msg, len);

            snd_buf_.insert(seg);

            msg += len;
            msglen -= len;
        }

        snd_us_ = 0;
        return 0;
    }


    // ========================================================================================================
    // output 将需要发送的协议数据投递到 output_que中
    // ========================================================================================================
    int output(uint64_t now_us) {
        if (state_) {
            return -1;
        }

        now_us -= base_us_;

        PRUX_FRM    frms[RUX_RWND_MAX * 2];
        int         nfrms   = 0, i;

        PRUX_FRM    frm     = nullptr;
        uint8_t*    p       = nullptr;
        uint16_t    nleft   = 0;
        uint16_t    n;
        RUX_SEG     seg;

        // ACK
        if (ack_que_.size() > 0) {
            std::list<PRUX_ACK> ack_list;
            PRUX_ACK ack;

            ack_que_.get_all(&ack_list);

            auto ack_itr = ack_list.begin();
            while (ack_list.size() > 0) {
                if (frm && nleft < RUX_SEG_HDR_SIZE) {
                    frm->len = p - frm->raw;
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
                seg.cmd = RUX_CMD_ACK;
                seg.sn = ack->sn;
                seg.us = ack->us;
                n = seg.encode(p, nleft);
                p += n;
                nleft -= n;
                delete ack;
                ack_list.erase(ack_itr);
            }

            if (probe_) {
                probe_ = 0;
            }
        }

        // snd_buffer
        int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
        if (snd_wnd > 0) {
            std::list<PRUX_SEG> seg_list;
            if (snd_buf_.get_segs(snd_wnd, &seg_list, now_us)) {
                for (i = 0; i < nfrms; i++) {
                    delete frms[i];
                }

                if (frm) {
                    delete frm;
                }

                state_ = -1;
                return -1;
            }
            
            for (auto& item : seg_list) {
                if (frm && nleft < RUX_SEG_HDR_EX_SIZE + item->len) {
                    frm->len = p - frm->raw;
                    ASSERT(!frm->setup());
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    ASSERT(frm);
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                if (item->xmit == 0) {
                    item->rto = rto_;
                }
                else if (item->resend_us <= now_us) {
                    item->rto *= 1.3;
                }
                else if (item->fastack >= RUX_FAST_ACK) {
                    item->fastack = 0;
                    item->rto = rto_;
                }

                item->xmit++;
                item->us = now_us;
                item->resend_us = item->rto + now_us;

                if (!frm) {
                    frm = _new_frm();
                    ASSERT(frm);
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                n = item->encode(p, nleft);
                p += n;
                nleft -= n;
            }

            if (probe_ && seg_list.size()) {
                probe_ = 0;
            }
        }

        // check ping
        if (rmt_wnd_ == 0) {
            if (frm && nleft < RUX_SEG_HDR_SIZE) {
                frm->len = p - frm->raw;
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

            seg.sn = snd_nxt_;
            seg.us = now_us;
            seg.cmd = RUX_CMD_PIN;

            n = seg.encode(p, nleft);
            p += n;
            nleft -= n;

            if (probe_) {
                probe_ = 0;
            }
        }

        // pong
        if (probe_) {
            if (frm && nleft < RUX_SEG_HDR_SIZE) {
                frm->len = p - frm->raw;
                ASSERT(!frm->setup());
                frms[nfrms++] = frm;
                frm = _new_frm();
                p = frm->raw + RUX_FRM_HDR_SIZE;
                nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
            }

            if (!frm) {
                frm = _new_frm();
                p = frm->raw + RUX_FRM_HDR_SIZE;
                nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
            }

            seg.sn = snd_nxt_;
            seg.us = now_us;
            seg.cmd = RUX_CMD_PON;
            n = seg.encode(p, nleft);
            p += n;
            nleft -= n;
            probe_ = 0;
        }

        if (nleft == 0) {
            ASSERT(nfrms == 0 && !frm);
            return (int)snd_buf_.size();
        }
        
        frm->len = p - frm->raw;
        ASSERT(!frm->setup());
        frms[nfrms++] = frm;

        if (nfrms > 0) {
            ASSERT(output_que_.enqueue_bulk(frms, nfrms));
        }

        return (int)snd_buf_.size();
    }


    // ========================================================================================================
    // active 激活当前rux, 并重置当前rux
    // ========================================================================================================
    void active(uint64_t now_us) {
        ack_que_.clear();
        snd_buf_.clear();

        state_ = rttval_ = srtt_ = probe_ = 0;
        rmt_wnd_ = cwnd_ = RUX_SWND_MIN;
        rto_ = RUX_RTO_MIN;

        rcv_nxt_ = snd_nxt_ = last_us_ = 0;
        base_us_ = now_us;
    }


    int update(int64_t now_us) {
        if (snd_us_ == 0 || snd_us_ <= now_us) {
            return output((uint64_t)now_us);
        }

        return 0;
    }


private:
    PRUX_FRM _new_frm() {
        PRUX_FRM frm = new RUX_FRM;
        frm->rid = rid_;
        frm->wnd = rcv_buf_.size() > RUX_RWND_MAX ? 0 : RUX_RWND_MAX - rcv_buf_.size();
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
    uint64_t                                        last_us_;                   // 上一次sn
    int64_t                                         snd_us_;                    // 发送时间

    sockaddr_storage                                addr_;                      // 对端地址
    socklen_t                                       addrlen_;                   // 对端地址长度

    RUX_ACKQ                                        ack_que_;                   // ACK队列
    RUX_SBUF                                        snd_buf_;                   // 发送缓冲区
    std::map<uint64_t, PRUX_SEG>                    rcv_buf_;                   // 接收队列

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   &output_que_;               // io output queue 引用
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
