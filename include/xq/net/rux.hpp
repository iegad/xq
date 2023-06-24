#ifndef __XQ_NET_RUX__
#define __XQ_NET_RUX__


#include <map>
#include "xq/net/udx.hpp"


namespace xq {
namespace net {


/* rux property */
constexpr int      RUX_FRM_EX_SIZE     = 10;
constexpr int      RUX_SEG_HDR_SIZE    = 13;                                                                    // cmd[1] + sn[6] + us[6];
constexpr int      RUX_SEG_HDR_EX_SIZE = 3;                                                                     // len[2] + frg[1]
constexpr int      RUX_MSS             = UDP_MTU - RUX_FRM_EX_SIZE - RUX_SEG_HDR_SIZE - RUX_SEG_HDR_EX_SIZE;    // RUX Maximum segment size
constexpr int      RUX_CMD_ACK         = 0x11;                                                                  // CMD: ACK
constexpr int      RUX_CMD_PIN         = 0x12;                                                                  // CMD: Heart's beat: Ping
constexpr int      RUX_CMD_CON         = 0x13;                                                                  // CMD: Connection
constexpr int      RUX_CMD_PSH         = 0x14;                                                                  // CMD: Push
constexpr int      RUX_RID_MAX         = 100000;                                                                // Maximum rux id
constexpr int      RUX_RWND_MAX        = 128;                                                                   // Maximum receive window size
constexpr int      RUX_SWND_MAX        = RUX_RWND_MAX / 8;                                                      // Maximum send window size
constexpr int      RUX_SWND_MIN        = 1;                                                                     // Minimun send window size
constexpr uint64_t RUX_SN_MAX          = 0x0000FFFFFFFFFFFF;                                                    // Maximum sequnce number
constexpr uint64_t RUX_US_MAX          = 0x0000FFFFFFFFFFFF;                                                    // Maximum timestamp(us)
constexpr int      RUX_FRM_MAX         = 92;                                                                    // Maximum fragment size
constexpr int      RUX_MSG_MAX         = RUX_FRM_MAX * RUX_MSS;                                                 // Maximum signle massage's length
constexpr int      RUX_RTO_MIN         = 200 * 1000;                                                            // RTO MIN 200ms
constexpr int      RUX_RTO_MAX         = 1000 * 1000 * 30;                                                      // RTO MAX 30s
constexpr int      RUX_TIMEOUT         = RUX_RTO_MAX * 2 * 10;                                                  // TIMEOUT: 10 min
constexpr int      RUX_FAST_ACK        = 3;
constexpr int      RUX_XMIT_MAX        = 10;
constexpr int      RUX_SSTHRESH_INIT   = 2;


class Rux {
public:
    typedef Rux* ptr;
    typedef std::mutex LockType;


    struct Segment {
        typedef Segment* ptr;

        uint8_t  cmd = 0;             // command
        uint64_t sn  = 0;             // sequence number
        uint64_t us  = 0;             // timestamp(us)

        uint16_t len = 0;             // segment's length
        uint8_t  frg = 0;             // fragment
        uint8_t  data[RUX_MSS] = {};  // data: 1424

        /* META */
        uint8_t  fastack   = 0;       //
        uint16_t xmit      = 0;       //
        uint32_t rto       = 0;       //
        uint64_t resend_us = 0;       //


        Segment()
        {}


        Segment(uint8_t cmd, uint64_t sn, uint16_t len, uint8_t frg, const uint8_t *data)
          : cmd(cmd)
          , sn(sn)
          , len(len)
          , frg(frg) {
            ASSERT(len <= RUX_MSS);
            ASSERT(data && len > 0 && len <= RUX_MSS);
            ::memcpy(this->data, data, len);
        }


        int decode(const uint8_t* buf, int buflen) {
            int ret = -1;

            do {
                if (!buf || buflen < RUX_SEG_HDR_SIZE) {
                    break;
                }

                const uint8_t* p = buf;

                p += u8_decode(p, &cmd);
                if (cmd < RUX_CMD_ACK || cmd > RUX_CMD_PSH) {
                    break;
                }

                p += u48_decode(p, &sn);
                if (sn > RUX_SN_MAX) {
                    break;
                }

                p += u48_decode(p, &us);
                if (us > RUX_US_MAX) {
                    break;
                }

                if (cmd == RUX_CMD_CON || cmd == RUX_CMD_PSH) {
                    if (buflen < RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE) {
                        break;
                    }

                    p += u16_decode(p, &len);
                    if (len > RUX_MSS) {
                        break;
                    }

                    if (buflen < len + RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE) {
                        break;
                    }

                    p += u8_decode(p, &frg);
                    if (frg > RUX_FRM_MAX) {
                        break;
                    }

                    ::memcpy(data, p, len);
                    p += len;
                }

                ret = int(p - buf);
            } while (0);

            return ret;
        }


        int encode(uint8_t* buf, int buflen) {
            ASSERT(buf && buflen > RUX_SEG_HDR_SIZE + RUX_FRM_EX_SIZE);

            int ret = -1;

            do {
                if (cmd < RUX_CMD_ACK || cmd > RUX_CMD_PSH ||
                    ((cmd == RUX_CMD_PSH || cmd == RUX_CMD_CON) && buflen < RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE + len) ||
                    sn > RUX_SN_MAX ||
                    us > RUX_US_MAX ||
                    len > RUX_MSS ||
                    frg > RUX_FRM_MAX) {
                    break;
                }

                uint8_t* p = buf;
                p += u8_encode(cmd, p);
                p += u48_encode(sn, p);
                p += u48_encode(us, p);
                if (len > 0) {
                    p += u16_encode(len, p);
                    p += u8_encode(frg, p);
                    ::memcpy(p, data, len);
                    p += len;
                }

                ret = int(p - buf);
            } while (0);
            
            return ret;
        }
    }; // struct Segment;


    typedef std::map<uint64_t, Segment::ptr> RcvBuf;
    typedef std::map<uint64_t, Segment::ptr> SndBuf;
    typedef std::map<uint64_t, uint64_t>     AckQue;


    Rux(uint32_t rid, uint64_t now_us, FrameQueue& snd_que)
        : rid_(rid)
        , base_us_(now_us)
        , snd_que_(snd_que) {
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

        ack_que_.clear();
    }


    void set_rmt_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        lkr_.lock();
        _set_remote_addr(addr, addrlen);
        lkr_.unlock();
    }


    int qid() const {
        return qid_;
    }


    void set_qid(int qid) {
        qid_ = qid;
    }


    uint32_t rid() const {
        return rid_;
    }


    int state() const {
        return state_;
    }


    void set_state(int state) {
        ASSERT(state == 0 || state == -1 || state == 1);
        state_ = state;
    }


    int input(Frame::ptr pfm) {
        ASSERT(pfm && pfm->ex == this);

        int ret     = -1;
        int err     = 0;
        int datalen = pfm->len;

        uint8_t* p       = pfm->raw;
        uint64_t now_us  = pfm->time_us - base_us_;
        uint64_t last_us = 0;

        // Frame ex
        uint32_t rid = 0;
        uint8_t  rmt_wnd = 0;
        uint64_t una = 0;

        Segment::ptr pseg;
        int n, update_rmt_wnd = 0;
        uint64_t sn, us;

        SndBuf::iterator itr;

        n = u24_decode(p, &rid);
        p += n;
        datalen -= n;

        n = u8_decode(p, &rmt_wnd);
        if (rmt_wnd == 0 || rmt_wnd > RUX_RWND_MAX) {
            return -1;
        }
        p += n;
        datalen -= n;

        n = u48_decode(p, &una);
        if (una > RUX_SN_MAX) {
            return -1;
        }
        p += n;
        datalen -= n;

        lkr_.lock();

        // update una
        while (snd_buf_.size()) {
            itr = snd_buf_.begin();
            if (itr->first >= una) {
                break;
            }

            _update_ack(itr->second, now_us);

            delete itr->second;
            snd_buf_.erase(itr);

            if (!update_rmt_wnd) {
                update_rmt_wnd = 1;
            }
        }

        do {
            while (datalen > 0) {
                pseg = new Segment;

                // -------------------- Step 1: 解析 Segment --------------------
                n = pseg->decode(p, datalen);
                if (n < 0) {
                    delete pseg;
                    err = 1;
                    break;
                }

                sn = pseg->sn;
                us = pseg->us;

                if (last_us < us) {
                    last_us = us;
                }

                // -------------------- Step 2: 消息分发 --------------------
                switch (pseg->cmd) {

                case RUX_CMD_ACK: {
                    if (sn >= snd_nxt_) {
                        delete pseg;
                        err = 1;
                        break;
                    }

                    itr = snd_buf_.find(sn);
                    if (snd_buf_.end() != itr) {
                        _update_ack(itr->second, now_us);

                        delete itr->second;
                        snd_buf_.erase(itr);

                        if (!update_rmt_wnd) {
                            update_rmt_wnd = 1;
                        }
                    }

                    delete pseg;
                }break;

                case RUX_CMD_PIN: {
                    delete pseg;
                }break;

                case RUX_CMD_PSH: {
                    if (sn == 0 && snd_nxt_ == 0) {
                        delete pseg;
                        err = 1;
                        break;
                    }

                    if (sn >= rcv_nxt_ && rcv_buf_.count(sn) == 0) {
                        rcv_buf_.insert(std::make_pair(sn, pseg));
                    }
                    else {
                        delete pseg;
                    }

                    if (ack_que_.count(sn) == 0) {
                        ack_que_.insert(std::make_pair(sn, us));
                    }

                    if (!update_rmt_wnd) {
                        update_rmt_wnd = 1;
                    }
                }break;

                case RUX_CMD_CON: {
                    if (sn != 0) {
                        delete pseg;
                        err = 1;
                        break;
                    }

                    if (rcv_buf_.count(sn) == 0) {
                        _reset(pfm->time_us);
                        rcv_buf_.insert(std::make_pair(sn, pseg));
                    }
                    else {
                        delete pseg;
                    }

                    if (ack_que_.count(sn) == 0) {
                        ack_que_.insert(std::make_pair(sn, us));
                    }

                    if (!update_rmt_wnd) {
                        update_rmt_wnd = 1;
                    }
                }break;

                default: {
                    delete pseg;
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

            // Step 4: 拥塞控制
            if (update_rmt_wnd) {
                rmt_wnd_ = rmt_wnd;
                if (rmt_wnd > cwnd_) {
                    cwnd_ = 12;
                }
            }

            int64_t diff = pfm->time_us - last_rcv_us_;
            if (diff > 100000) {
                if (delivered_ > 0) {
                    bw_ = delivered_ * 1000000 / diff;
                    DLOG("min_rtt: %llu, delivered: %llu, diff: %lld, bandwidth: %llu Bytes/s\n", min_rtt_, delivered_, diff, bw_);
                    delivered_ = 0;
                }
                
                last_rcv_us_ = pfm->time_us;
            }

            _set_remote_addr(&pfm->name, pfm->namelen);
            ret = 0;
        } while (0);
        lkr_.unlock();

        return ret;
    }


    int recv(uint8_t* msg) {
        int      n  = 0;
        int      ok = 0;
        uint8_t* p  = msg;

        int i;
        uint16_t len;
        RcvBuf::iterator itr;
        Segment::ptr psegs[RUX_FRM_MAX], pseg;

        lkr_.lock();
        uint64_t nxt = rcv_nxt_;
        itr = rcv_buf_.begin();

        while (itr != rcv_buf_.end()) {
            pseg = itr->second;

            if (pseg->sn != nxt) {
                break;
            }

            nxt++;
            psegs[n++] = pseg;
            if (!pseg->frg) {
                ok = 1;
                break;
            }
            ++itr;
        }

        if (ok) {
            for (i = 0; i < n; i++) {
                pseg = psegs[i];
                len = pseg->len;
                if (len) {
                    ::memcpy(p, pseg->data, len);
                    p += len;
                }
                delete pseg;
            }

            rcv_buf_.erase(rcv_buf_.begin(), ++itr);
            rcv_nxt_ = nxt;
        }

        lkr_.unlock();
        return (int)(p - msg);
    }


    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        int ret = -1;
        int len, i, n;
        uint8_t cmd;
        Segment::ptr seg;

        lkr_.lock();
        do {
            if (state_ == -1) {
                break;
            }

            for (i = 1, n = msglen > RUX_MSS ? (msglen + RUX_MSS - 1) / RUX_MSS : 1; i <= n; i++) {
                len = i == n ? msglen : RUX_MSS;
                
                if (state_ == 0) {
                    cmd = RUX_CMD_CON;
                    state_ = 1;
                }
                else {
                    cmd = RUX_CMD_PSH;
                }

                seg = new Segment(cmd, snd_nxt_++, len, n - i, msg);
                snd_buf_.insert(std::make_pair(seg->sn, seg));
                msg += len;
                msglen -= len;
            }

            ret = 0;
        } while (0);
        lkr_.unlock();

        return ret;
    }


    int output(uint64_t now_us) {
        constexpr int LOST_LIMIT = 50;
        constexpr int FRAME_MAX = RUX_RWND_MAX << 1;
        constexpr uint64_t LOST_INTERVAL = 1000 * 1000 * 10;

        int already_snd = 0;
        int err         = 0;
        int needsnd     = 0;
        int npfms       = 0;
        int nleft       = 0;
        int ret         = -1;
        int i, n, wnd;
        

        uint8_t* p     = nullptr;
        Frame::ptr pfm = nullptr;

        Segment::ptr pseg;
        
        Segment seg;
        Frame::ptr pfms[FRAME_MAX + 1];

        AckQue::iterator ack_itr;
        SndBuf::iterator psh_itr;

        lkr_.lock();

        uint64_t una = rcv_nxt_;

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
            while (ack_que_.size() > 0 && npfms < FRAME_MAX) {
                ack_itr = ack_que_.begin();
                if (pfm && nleft < RUX_SEG_HDR_SIZE + RUX_FRM_EX_SIZE) {
                    pfm->len = uint16_t(p - pfm->raw);
                    pfms[npfms++] = pfm;

                    pfm = new Frame(&addr_, addrlen_);
                    ASSERT(pfm);
                    p = pfm->raw;
                    nleft = UDP_MTU;

                    n = u24_encode(rid_, p);
                    p += n;
                    nleft -= n;

                    wnd = RUX_RWND_MAX - (int)rcv_buf_.size();
                    if (wnd <= 0) {
                        wnd = 1;
                    }

                    n = u8_encode((uint8_t)wnd, p);
                    p += n;
                    nleft -= n;

                    n = u48_encode(una, p);
                    p += n;
                    nleft -= n;
                }

                if (!pfm) {
                    pfm = new Frame(&addr_, addrlen_);
                    ASSERT(pfm);
                    p = pfm->raw;
                    nleft = UDP_MTU;

                    n = u24_encode(rid_, p);
                    p += n;
                    nleft -= n;

                    wnd = RUX_RWND_MAX - (int)rcv_buf_.size();
                    if (wnd <= 0) {
                        wnd = 1;
                    }

                    n = u8_encode((uint8_t)wnd, p);
                    p += n;
                    nleft -= n;

                    n = u48_encode(una, p);
                    p += n;
                    nleft -= n;
                }

                if (ack_itr->first >= una) {
                    seg.cmd = RUX_CMD_ACK;
                    seg.sn = ack_itr->first;
                    seg.us = ack_itr->second;
                    n = seg.encode(p, nleft);
                    ASSERT(n == RUX_SEG_HDR_SIZE);
                    p += n;
                    nleft -= n;
                }

                ack_que_.erase(ack_itr++);
            }

            // PSH
            int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
            psh_itr = snd_buf_.begin();
            while (already_snd < snd_wnd && psh_itr != snd_buf_.end() && npfms < FRAME_MAX) {
                pseg = psh_itr->second;

                if (pfm && nleft < RUX_FRM_EX_SIZE + RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE + pseg->len) {
                    pfm->len = uint16_t(p - pfm->raw);
                    pfms[npfms++] = pfm;

                    pfm = new Frame(&addr_, addrlen_);
                    ASSERT(pfm);
                    p = pfm->raw;
                    nleft = UDP_MTU;

                    n = u24_encode(rid_, p);
                    p += n;
                    nleft -= n;

                    wnd = RUX_RWND_MAX - (int)rcv_buf_.size();
                    if (wnd <= 0) {
                        wnd = 1;
                    }

                    n = u8_encode((uint8_t)wnd, p);
                    p += n;
                    nleft -= n;

                    n = u48_encode(una, p);
                    p += n;
                    nleft -= n;
                }

                if (pseg->xmit == 0) {
                    pseg->rto = rto_;
                    needsnd = 1;
                }
                else if (pseg->xmit >= RUX_XMIT_MAX) {
                    DLOG("state changed, rux xmit limit\n");
                    state_ = -1;
                    for (i = 0; i < npfms; i++) {
                        delete pfms[i];
                    }

                    if (pfm) {
                        delete pfm;
                    }

                    err = 1;
                    break;
                }
                else {
                    if (pseg->resend_us <= now_us) {
                        DLOG("-------------------------------------------------------: %lu\n", pseg->sn);
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

                    if (!pfm) {
                        pfm = new Frame(&addr_, addrlen_);
                        ASSERT(pfm);
                        p = pfm->raw;
                        nleft = UDP_MTU;

                        n = u24_encode(rid_, p);
                        p += n;
                        nleft -= n;

                        wnd = RUX_RWND_MAX - (int)rcv_buf_.size();
                        if (wnd <= 0) {
                            wnd = 1;
                        }

                        n = u8_encode((uint8_t)wnd, p);
                        p += n;
                        nleft -= n;

                        n = u48_encode(una, p);
                        p += n;
                        nleft -= n;
                    }

                    n = pseg->encode(p, nleft);
                    ASSERT(n == pseg->len + RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE);
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

        if (ret != -1 && pfm) {
            pfm->len = uint16_t(p - pfm->raw);
            pfms[npfms++] = pfm;

            if (npfms > 0) {
                if(!snd_que_.enqueue_bulk(pfms, npfms)) {
                    DLOG("EN_QUE FAILED: ..................\n");
                }
            }
        }

        return ret;
    }


private:
    void _reset(uint64_t now_us) {
        ack_que_.clear();

        while (rcv_buf_.size()) {
            auto itr = rcv_buf_.begin();
            delete itr->second;
            rcv_buf_.erase(itr);
        }

        while (snd_buf_.size()) {
            auto itr = snd_buf_.begin();
            delete itr->second;
            snd_buf_.erase(itr);
        }
        
        base_us_ = now_us;
        cwnd_ = RUX_SWND_MIN;
        ssthresh_ = RUX_SSTHRESH_INIT;
        rmt_wnd_ = RUX_SWND_MIN;
        min_rtt_ = 0;
        bw_ = 0;    
        delivered_ = 0;

        rto_ = RUX_RTO_MIN;
        srtt_ = 0;
        rttval_ = 0;
        nlost_ = 0;

        rcv_nxt_ = 0;
        snd_nxt_ = 0;
        last_rcv_us_ = 0;
    }

    
    void _set_remote_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
        }

        if (::memcmp(&addr_, addr, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
        }
    }


    void _update_ack(Segment::ptr pseg, uint64_t now_us) {
        int64_t rtt = int64_t(now_us - pseg->us) + 1;

        if (rtt > 0) {
            if (srtt_ == 0) {
                srtt_ = rtt;
                rttval_ = rtt >> 1;
            }
            else {
                int64_t delta = rtt - srtt_;
                if (delta < 0) {
                    delta = -delta;
                }

                rttval_ = (3 * rttval_ + delta) >> 2;
                srtt_ = (7 * srtt_ + rtt) >> 4;
            }

            rto_ = MID(RUX_RTO_MIN, srtt_ + (rttval_ << 2), RUX_RTO_MAX);

            if (rtt < min_rtt_ || min_rtt_ == 0) {
                min_rtt_ = rtt;
            }
        }

        delivered_ += pseg->len;
    }


    uint32_t    rid_;       // rux id
    uint64_t    base_us_;   // 启始时间(微秒)
    FrameQueue& snd_que_;   // io output queue 引用

    uint8_t  cwnd_      = RUX_SWND_MIN;
    uint8_t  ssthresh_  = RUX_SSTHRESH_INIT;
    uint8_t  rmt_wnd_   = RUX_SWND_MIN;
    uint64_t min_rtt_   = 0;
    uint64_t bw_        = 0;    // 带宽
    uint64_t delivered_ = 0;
    
    int rto_    = RUX_RTO_MIN;   // RTO
    int srtt_   = 0;             // smooth RTT
    int rttval_ = 0;          
    int nlost_  = 0;
    
    uint64_t rcv_nxt_     = 0;    // 下一次接收 sn
    uint64_t snd_nxt_     = 0;    // 下一次发送 sn
    uint64_t last_rcv_us_ = 0;    // 最后一次接收时间

    socklen_t        addrlen_ = sizeof(sockaddr_storage);
    sockaddr_storage addr_    = {};

    std::atomic<int> state_ = -1;
    std::atomic<int> qid_   = -1;  // rux_que index

    LockType lkr_;
    AckQue   ack_que_;             // ACK队列
    SndBuf   snd_buf_;             // 发送缓冲区
    RcvBuf   rcv_buf_;             // 接收缓冲区

    
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
