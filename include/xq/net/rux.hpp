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
#include "xq/net/timer.hpp"


namespace xq {
namespace net {


// ############################################################################################################
// Rux 协议
// ############################################################################################################
class Rux {
public:
    typedef Rux* ptr;


    // ========================================================================================================
    // 构造函数
    // @rid:        rux id
    // @now_us:     系统时间(微秒)
    // @output_que: io output队列
    // @update_que: update 队列
    // ========================================================================================================
    explicit Rux(
        uint32_t rid, 
        uint64_t now_us, 
        moodycamel::BlockingConcurrentQueue<PRUX_FRM>& output_que, 
        moodycamel::BlockingConcurrentQueue<Rux::ptr>* update_que = nullptr, 
        TimerScheduler<Rux>* ts = nullptr)
        : rid_(rid)
        , cwnd_(RUX_SWND_MIN)
        , ssthresh_(RUX_SSTHRESH_INIT)
        , rmt_wnd_(RUX_SWND_MIN)

        , probe_(0)
        , state_(-1)
        , qid_(-1)
        , rto_(RUX_RTO_MIN)
        , srtt_(0)
        , rttval_(0)
        , base_us_(now_us)

        , rcv_nxt_(0)
        , snd_nxt_(0)
        , last_us_(0)
        , last_rcv_us_(0)

        , addr_({0,{0},0})
        , addrlen_(sizeof(addr_))

        , ts_(ts)
        , output_que_(output_que)
        , update_que_(update_que) {
        ASSERT(rid > 0 && rid <= RUX_RID_MAX);
    }


    ~Rux() {
        snd_buf_.clear();
        rcv_buf_.clear();
        ack_que_.clear();
    }


    // ========================================================================================================
    // rux remote 地址
    // ========================================================================================================
    inline sockaddr_storage* addr() {
        return &addr_;
    }


    // ========================================================================================================
    // rux remote 地址长度
    // ========================================================================================================
    inline socklen_t* addrlen() {
        return &addrlen_;
    }


    inline int qid() const {
        return qid_;
    }


    inline void set_qid(int qid) {
        state_ = 0;
        qid_ = qid;
    }


    inline uint32_t rid() const {
        return rid_;
    }


    inline int state() const {
        return state_;
    }


    inline void set_state(int state) {
        ASSERT(state == 0 || state == -1);
        state_ = state;
    }


    // ========================================================================================================
    // input 用于处理原始UDP数据
    // ========================================================================================================
    int input(PRUX_FRM frm) {
        int         datalen     = frm->len - RUX_FRM_HDR_SIZE;  // Frame Payload 长度
        int         n;
        uint8_t*    p           = frm->raw + RUX_FRM_HDR_SIZE;  // Frame Payload 数据
        uint64_t    last_us     = 0;                            // 当前 Frame 中最大的时间(微秒)
        uint64_t    now_us      = frm->time_us - base_us_;      // 当前时间
        PRUX_SEG    seg;

        DLOG("[INPUT] Frame una[%lu]\n", frm->una);
        snd_buf_.update_una(frm->una);

        while (datalen > 0) {
            seg = new RUX_SEG;
            
            // -------------------- Step 1: 解析 Segment --------------------
            n = seg->decode(p, datalen);
            if (n < 0) {
                delete seg;
                state_ = -1;
                return -1;
            }

            if (last_us < seg->us) {
                last_us = seg->us;
            }

            // -------------------- Step 2: 消息分发 --------------------
            DLOG("[INPUT] seg SN[%lu] CMD[%d]\n", seg->sn, seg->cmd);
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

                snd_buf_.update_ack(seg->sn);

                /* ---------------------
                 * 这里可能会出现负值.
                 * 例如:
                 *    1, 发送端发 psh[sn:1, us:1] 的包.
                 *    2, 对端收到后响应 ack[sn:1, us:1], 但是该响但在网络中滞留.
                 *    3, 发送端触发重传(可能是超时重传, 也可能是快速重传) re psh[sn:1, ts:2].
                 *    4, 发送端此时收到滞留的 ack[sn:1, ts: 1], 这时就会出现RTT时间负数的情况.
                 */
                int rtt = (int)(now_us - seg->us + 1);
                delete seg;

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
            }break;

            /* ------------- RUX_CMD_PIN -------------
             * 1, 将探针标志置为 1;
             * 2, 将snd_us_ 置0;
             * 
             * 下一次 update 时发送 RUX_CMD_PON segment
             */
            case RUX_CMD_PIN: {
                probe_ = 1;
                delete seg;
            }break;

            /* ------------- RUX_CMD_PSH -------------
             * 1, 检查seg->sn范围;
             * 2, 检查该包是否处理过;
             */
            case RUX_CMD_PSH: {
                if (seg->sn == 0) {
                    delete seg;
                    return -1;
                }

                ack_que_.insert(seg->sn, seg->us);

                if (seg->sn >= rcv_nxt_ && rcv_buf_.count(seg->sn) == 0) {
                    seg->rid = frm->rid;
                    seg->time_us = frm->time_us;
                    seg->addr = &frm->name;
                    seg->addrlen = frm->namelen;
                    rcv_buf_.insert(seg);
                    if (rcv_buf_.size() > RUX_SWND_MAX) {
                        DLOG("###################################################################### %u\n", rcv_buf_.size());
                    }
                }
                else {
                    delete seg;
                }

                if (update_que_) {
                    update_que_->enqueue(this);
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

                if (rcv_buf_.count(seg->sn) > 0) {
                    ack_que_.insert(seg->sn, seg->us);
                    delete seg;
                    return 0;
                }

                seg->rid = frm->rid;
                seg->time_us = frm->time_us;
                seg->addr = &frm->name;
                seg->addrlen = frm->namelen;
                reset(seg->time_us);
                rcv_buf_.insert(seg);
                ack_que_.insert(seg->sn, seg->us);
                if (update_que_) {
                    update_que_->enqueue(this);
                }
            }break;

            default: delete seg; break;
            } // switch

            p += n;
            datalen -= n;
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

        last_rcv_us_ = frm->time_us;

        // Step 5: 设置对端地址
        return 0;
    }


    // ========================================================================================================
    // recv 从读缓冲区获取rux message
    // @return 读缓冲区中有数据返回 msg数据长度, 否则返回 0
    // ========================================================================================================
    int recv(uint8_t* msg) {
        return rcv_buf_.get_msg(msg, &rcv_nxt_);
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

        if (update_que_) {
            update_que_->enqueue(this);
        }
        return 0;
    }


    // ========================================================================================================
    // output 将需要发送的协议数据投递到 output_que中
    // ========================================================================================================
    int output(uint64_t now_us) {
        if (last_rcv_us_ > 0 && last_rcv_us_ + RUX_TIMEOUT < now_us) {
            state_ = -1;
        }

        if (state_ == -1) {
            return -1;
        }

        now_us -= base_us_;

        int nfrms = 0, nleft = 0, i, n;

        PRUX_FRM frms[RUX_RWND_MAX * 2];
        PRUX_FRM frm     = nullptr;
        uint8_t* p       = nullptr;
        

        // ACK
        if (ack_que_.size() > 0) {
            std::list<PRUX_ACK> ack_list;
            PRUX_ACK ack;
            RUX_SEG  seg;

            ack_que_.move_in(&ack_list);

            while (ack_list.size() > 0) {
                auto ack_itr = ack_list.begin();
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
                if (ack->sn >= frm->una) {
                    seg.cmd = RUX_CMD_ACK;
                    seg.sn = ack->sn;
                    seg.us = ack->us;
                    n = seg.encode(p, nleft);
                    p += n;
                    nleft -= n;
                }
                
                delete ack;
                ack_list.erase(ack_itr++);
            }

            if (probe_) {
                probe_ = 0;
            }
        }

        // snd_buffer
        int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
        if (snd_wnd > 0) {
            std::list<PRUX_SEG> seg_list;
            snd_buf_.get_segs(&seg_list, snd_wnd, now_us);
            
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
                else if (item->xmit >= RUX_XMIT_MAX) {
                    // 当重传到上限时, 将该rux 视频无效
                    state_ = -1;
                    DLOG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> SN[%lu] resend LIMIT\n", item->sn);
                    for (i = 0; i < nfrms; i++) {
                        delete frms[i];
                    }

                    if (frm) {
                        delete frm;
                    }

                    return -1;
                }
                else if (item->resend_us <= now_us) {
                    DLOG(" ---------------------------------------------------------- SN[%lu] timeout resend\n", item->sn);
                    item->rto *= 1.3;
                    ssthresh_ = cwnd_ / 2;
                    cwnd_ = 1;
                }
                else if (item->fastack >= RUX_FAST_ACK) {
                    DLOG(" ========================================================== SN[%lu] fastack resend\n", item->sn);
                    item->fastack = 0;
                    item->rto = rto_;

                    cwnd_ /= 2;
                    ssthresh_ = cwnd_;
                }

                item->xmit++;
                item->us = now_us;
                item->resend_us = item->rto + now_us;
                if (ts_) {
                    ts_->create_timer_at(item->resend_us, this);
                }

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
            RUX_SEG seg;

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
    inline void reset(uint64_t now_us) {
        ack_que_.clear();
        rcv_buf_.clear();
        snd_buf_.clear();

        rttval_ = srtt_ = probe_ = 0;
        rmt_wnd_ = cwnd_ = RUX_SWND_MIN;
        rto_ = RUX_RTO_MIN;

        rcv_nxt_ = snd_nxt_ = last_us_ = 0;
        last_rcv_us_ = base_us_ = now_us;
    }


    inline void set_remote_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
        }

        if (::memcmp(&addr_, addr, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
        }
    }


    inline void update() {
        if (update_que_) {
            update_que_->enqueue(this);
        }
    }

    inline bool destory() {
        return false;
    }


private:
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

    uint8_t                                         probe_;                     // 探针
    int                                             state_;
    int                                             qid_;                       // rux_que index
    int                                             rto_;                       // RTO
    int                                             srtt_;                      // smooth RTT
    int                                             rttval_;                    
    uint64_t                                        base_us_;                   // 启始时间(微秒)
    
    uint64_t                                        rcv_nxt_;                   // 下一次接收 sn
    uint64_t                                        snd_nxt_;                   // 下一次发送 sn
    uint64_t                                        last_us_;                   // 上一次sn
    uint64_t                                        last_rcv_us_;               // 最后一次接收时间

    sockaddr_storage                                addr_;                      // 对端地址
    socklen_t                                       addrlen_;                   // 对端地址长度

    RUX_ACKQ                                        ack_que_;                   // ACK队列
    RUX_SBUF                                        snd_buf_;                   // 发送缓冲区
    RUX_RBUF                                        rcv_buf_;                   // 接收队列

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   &output_que_;               // io output queue 引用
    moodycamel::BlockingConcurrentQueue<Rux::ptr>   *update_que_;               // update 队列 引用
    TimerScheduler<Rux>                             *ts_;
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
