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

        , addr_({0,{0},0})
        , addrlen_(sizeof(addr_))

        , output_que_(output_que) 
    {}


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
        uint64_t    last_us     = 0;

        while (datalen > 0) {
            seg = new RUX_SEG;
            // Step 1: 解析 RuxSegment
            n = seg->decode(p, datalen);
            if (n < 0) {
                delete seg;
                std::printf("-------------------err\n");
                return -1;
            }

            if (last_us < seg->us) {
                last_us = seg->us;
            }

            // Step 2: 消息分发
            DLOG("[INPUT] 2, seg SN[%llu] CMD[%d]\n", seg->sn, seg->cmd);
            switch (seg->cmd) {

            case RUX_CMD_ACK: {
                seg->time_us = frm->time_us;
                ret = _ack_handle(seg);
            }break;

            case RUX_CMD_PIN: {
                probe_ = 1;
                ret = 0;
            }break;

            case RUX_CMD_PON: {
                if (seg->sn == rcv_nxt_) rmt_wnd_ = frm->wnd;
                ret = 0;
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

            default: { 
                delete seg; ret = -1;
            }break;
            } // switch

            if (ret) {
                return -1;
            }

            p += n;
            datalen -= n;
        }

        // Step 3: 设置新的 remote wnd
        if (last_us_ <= last_us) {
            last_us_ = last_us;
            rmt_wnd_ = frm->wnd;
            if (rmt_wnd_ == 0) {
                DLOG("### rmt_wnd is zero\n");
            }
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
        uint64_t nxt = rcv_nxt_;
        std::map<uint64_t, PRUX_SEG>::iterator itr;
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

            DLOG("[RECV] 3.1, [PSH] rcv_buf.size [%llu]\n", rcv_buf_.size());
            return p - msg;
        }

        DLOG("[RECV] 3.1, [PSH] rcv_buf.size [%llu]\n", rcv_buf_.size());
        return 0;
    }


    // ---------------------------------------------
    // send 将msg 转换为 segment 并投递发送缓冲区
    // ---------------------------------------------
    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        if (snd_buf_.size() >= RUX_SWND_MAX) {
            return -1;
        }

        int n = msglen > RUX_MSS ? (msglen + RUX_MSS - 1) / RUX_MSS : 1;
        PRUX_SEG seg;
        uint64_t now_us = sys_clock();

        for (int i = 1; i <= n; i++) {
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

        return 0;
    }


    // ---------------------------------------------
    // output 将需要发送的协议数据投递到 output_que中
    // ---------------------------------------------
    int output(uint64_t now_us) {
        now_us -= base_us_;

        PRUX_FRM frm = _new_frm();

        uint8_t* p = frm->raw + RUX_FRM_HDR_SIZE;
        uint16_t nleft = RUX_MTU - RUX_FRM_HDR_SIZE, n;
        RUX_SEG seg;

        PRUX_FRM frms[128];
        int nfrms = 0;

        // ACK
        std::list<PRUX_ACK> ack_list;
        ack_que_.get_all(&ack_list);
        if (ack_list.size() > 0) {
            PRUX_ACK ack;
            auto ack_itr = ack_list.begin();
            while (ack_list.size() > 0) {
                if (nleft < RUX_SEG_HDR_SIZE) {
                    frm->len = p - frm->raw;
                    frm->setup();
                    frms[nfrms++] = frm;
                    frm = _new_frm();
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
        }

        // snd_buffer
        int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
        if (snd_wnd > 0) {
            std::list<PRUX_SEG> seg_list;
            snd_buf_.get_segs(snd_wnd, &seg_list, now_us);

            for (auto& item : seg_list) {
                if (nleft < RUX_SEG_HDR_EX_SIZE + item->len) {
                    frm->len = p - frm->raw;
                    frm->setup();
                    frms[nfrms++] = frm;
                    frm = _new_frm();
                    p = frm->raw + RUX_FRM_HDR_SIZE;
                    nleft = RUX_MTU - RUX_FRM_HDR_SIZE;
                }

                if (item->xmit == 0) {
                    item->rto = rto_;
                }
                else if (item->resend_us <= now_us) {
                    DLOG("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^&&&&***(((((-----------: SN[%llu]\tXMIT[%d]\tresnd_us[%llu]\tnow_us[%llu]\n", 
                        item->sn, item->xmit, item->resend_us, now_us);
                    item->rto *= 1.3;
                }
                else if (item->fastack >= RUX_FAST_ACK) {
                    item->fastack = 0;
                    item->rto = rto_;
                }

                item->xmit++;
                item->us = now_us;
                item->resend_us = item->rto + now_us;

                n = item->encode(p, nleft);
                p += n;
                nleft -= n;
            }
        }

        if (rmt_wnd_ == 0) {
            if (nleft < RUX_SEG_HDR_SIZE) {
                frm->len = p - frm->raw;
                frm->setup();
                frms[nfrms++] = frm;
                frm = _new_frm();
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

        if (state_ == -1) {
            delete frm;
            return -1;
        }

        if (probe_) {
            if (nleft < RUX_SEG_HDR_SIZE) {
                frm->len = p - frm->raw;
                frm->setup();
                frms[nfrms++] = frm;
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

        if (nleft == RUX_MTU - RUX_FRM_HDR_SIZE) {
            ASSERT(nfrms == 0);
            delete frm;
        }
        else {
            frm->len = p - frm->raw;
            frm->setup();
            frms[nfrms++] = frm;
        }

        if (nfrms > 0) {
            ASSERT(output_que_.enqueue_bulk(frms, nfrms));
        }

        return (int)snd_buf_.size();
    }


    // ---------------------------------------------
    // active 激活当前rux, 并重置当前rux
    // ---------------------------------------------
    void active(uint64_t now_us) {
        ack_que_.clear();
        snd_buf_.clear();

        state_ = rttval_ = srtt_ = probe_ = 0;
        rmt_wnd_ = cwnd_ = RUX_SWND_MIN;
        rto_ = RUX_RTO_MIN;

        rcv_nxt_ = snd_nxt_ = last_us_ = 0;
        base_us_ = now_us;
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


    int _ack_handle(PRUX_SEG seg) {
        int ret = -1;

        do {
            // Step 1: 检查seg->sn, ACK sn 必需在发送窗口范围内
            DLOG("[INPUT] 2.1, [ACK] check seg->sn[%llu] range\n", seg->sn);
            if (seg->sn >= snd_nxt_) {
                break;
            }

            // Step 2: 从send buffer 中移除对应的 Segment
            snd_buf_.erase(seg->sn);

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

            // Step 5: 计算RTO
            rto_ = MID(RUX_RTO_MIN, srtt_ + 4 * rttval_, RUX_RTO_MAX);
            ret = 0;
        } while (0);

        delete seg;
        return ret;
    }


    int _con_handle(PRUX_SEG seg) {
        if (seg->sn != 0) {
            delete seg;
            return -1;
        }

        active(seg->time_us);
        ack_que_.insert(seg->sn, seg->us);
        rcv_buf_.insert(std::make_pair(seg->sn, seg));
        return 0;
    }


    int _psh_handle(PRUX_SEG seg) {
        DLOG("[INPUT] 2.1, [PSH] check seg->sn[%llu] range\n", seg->sn);
        if (seg->sn == 0 || seg->sn >= RUX_RWND_MAX + rcv_nxt_) {
            delete seg;
            return -1;
        }
        else if (seg->sn < rcv_nxt_) {
            delete seg;
            return 0;
        }

        ack_que_.insert(seg->sn, seg->us);
        if (rcv_buf_.count(seg->sn) == 0) {
            rcv_buf_.insert(std::make_pair(seg->sn, seg));
        }
        return 0;
    }


    int                                             state_;                       // 工作队列id
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

    sockaddr_storage                                addr_;                      // 对端地址
    socklen_t                                       addrlen_;                   // 对端地址长度


    RUX_ACKQ                                        ack_que_;                   // ACK队列
    RUX_SBUF                                        snd_buf_;                   // 发送缓冲区
    std::map<uint64_t, PRUX_SEG>                    rcv_buf_;        // 接收队列

    moodycamel::BlockingConcurrentQueue<PRUX_FRM>   &output_que_;               // io output queue 引用
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
