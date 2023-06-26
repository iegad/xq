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
constexpr int      RUX_RWND_MAX        = 64;                                                                   // Maximum receive window size
constexpr int      RUX_SWND_MAX        = 4;                                                                    // Maximum send window size
constexpr int      RUX_SWND_MIN        = 1;                                                                     // Minimun send window size
constexpr uint64_t RUX_SN_MAX          = 0x0000FFFFFFFFFFFF;                                                    // Maximum sequnce number
constexpr uint64_t RUX_US_MAX          = 0x0000FFFFFFFFFFFF;                                                    // Maximum timestamp(us)
constexpr int      RUX_FRM_MAX         = 92;                                                                    // Maximum fragment size
constexpr int      RUX_MSG_MAX         = RUX_FRM_MAX * RUX_MSS;                                                 // Maximum signle massage's length
constexpr int      RUX_RTO_MIN         = 500 * 1000;                                                            // RTO MIN 200ms
constexpr int      RUX_RTO_MAX         = 1000 * 1000 * 30;                                                      // RTO MAX 30s
constexpr int      RUX_TIMEOUT         = RUX_RTO_MAX * 2 * 10;                                                  // TIMEOUT: 10 min
constexpr int      RUX_FAST_ACK        = 3;
constexpr int      RUX_XMIT_MAX        = 20;
constexpr int      RUX_SSTHRESH_INIT   = 2;


class Rux {
public:
    typedef Rux* ptr;
    typedef SpinLock LockType;


    static int decode_hdr(const uint8_t* p, int datalen, uint32_t* rid, uint8_t* wnd, uint64_t* una) {
        ASSERT(p && datalen >= RUX_FRM_EX_SIZE);

        const uint8_t* ps = p;
        p += u24_decode(p, rid);
        if (*rid == 0 || *rid > RUX_RID_MAX) {
            return -1;
        }

        p += u8_decode(p, wnd);
        if (*wnd == 0 || *wnd > RUX_RWND_MAX) {
            return -1;
        }

        p += u48_decode(p, una);
        if (*una > RUX_SN_MAX) {
            return -1;
        }

        return int(p - ps);
    }


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

        int ret         = -1;
        int err         = 0;
        int datalen     = pfm->len;
        uint8_t* p      = pfm->raw;
        uint64_t now_us = pfm->time_us - base_us_;

        // Step 1: Decode rux header
        uint32_t rid = 0;
        uint8_t  rmt_wnd = 0;
        uint64_t una = 0;

        int n = decode_hdr(p, datalen, &rid, &rmt_wnd, &una);
        if (n < 0) {
            return -1;
        }

        p += n;
        datalen -= n;

        uint64_t sn, us;
        Segment::ptr pseg;

        lkr_.lock();

        // Step 2: Update una
        while (snd_buf_.size()) {
            auto itr = snd_buf_.begin();
            if (itr->first >= una) {
                break;
            }

            delete itr->second;
            snd_buf_.erase(itr);
        }

        // Step 3: Decode segments
        do {
            while (datalen > 0) {
                pseg = new Segment;

                n = pseg->decode(p, datalen);
                if (n < 0) {
                    delete pseg;
                    ASSERT(0);
                    err = 1;
                    break;
                }

                sn = pseg->sn;
                us = pseg->us;

                switch (pseg->cmd) {

                case RUX_CMD_ACK: {
                    if (sn >= snd_nxt_) {
                        delete pseg;
                        ASSERT(0);
                        err = 1;
                        break;
                    }

                    auto itr = snd_buf_.find(sn);
                    if (snd_buf_.end() != itr) {
                        int64_t rtt = int64_t(now_us - us + 1);
                        delete itr->second;
                        snd_buf_.erase(itr);

                        if (rtt > 0) {
                            if (srtt_ == 0) {
                                srtt_ = rtt;
                                rttval_ = rtt / 2;
                            }
                            else {
                                int64_t delta = rtt - srtt_;
                                if (delta < 0) {
                                    delta = -delta;
                                }

                                rttval_ = int(3 * rttval_ + delta) / 4;
                                srtt_ = int(7 * srtt_ + rtt) / 8;
                            }

                            rto_ = MID(RUX_RTO_MIN, srtt_ + (rttval_ * 4), RUX_RTO_MAX);
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
                        ASSERT(0);
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
                }break;

                case RUX_CMD_CON: {
                    if (sn != 0) {
                        delete pseg;
                        ASSERT(0);
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
                }break;

                default: {
                    delete pseg;
                    ASSERT(0);
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
            if (rmt_wnd > cwnd_ && cwnd_ < RUX_SWND_MAX) {
                if (cwnd_ < ssthresh_) {
                    cwnd_ += cwnd_;
                }
                else {
                    cwnd_++;
                }
            }
            rmt_wnd_ = rmt_wnd;

            if (ack_que_.size()) {
                Frame::ptr pfms[RUX_RWND_MAX];
                n = _flush_ack(pfms, RUX_RWND_MAX);
                if (n > 0) {
                    ASSERT(snd_que_.enqueue_bulk(pfms, n));
                }
            }

            last_rcv_us_ = pfm->time_us;
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
        int n   = 0;
        int ret = -1;

        int npfms = 0;
        Frame::ptr pfms[RUX_RWND_MAX];

        lkr_.lock();

        do {
            if (now_us > last_rcv_us_ && last_rcv_us_ > 0 && now_us > last_rcv_us_ && now_us - last_rcv_us_ > RUX_TIMEOUT) {
                state_ = -1;
            }

            if (state_ == -1) {
                break;
            }

            if (snd_buf_.size()) {
                now_us -= base_us_;
                n = _flush_psh(now_us, pfms, npfms);
                if (n < 0) {
                    break;
                }

                npfms += n;
            }

            ret = 0;
        } while (0);

        lkr_.unlock();

        if (ret == 0 && npfms > 0) {
            ASSERT(snd_que_.enqueue_bulk(pfms, npfms));
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

        rto_ = RUX_RTO_MIN;
        srtt_ = 0;
        rttval_ = 0;

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


    int _flush_ack(Frame::ptr *pfms, int npfms) {
        npfms = 0;

        uint8_t  wnd;
        int      nleft = UDP_MTU;
        uint64_t una   = rcv_nxt_;

        Frame::ptr pfm = new Frame(&addr_, addrlen_);
        ASSERT(pfm);
        uint8_t* p = pfm->raw;
        nleft = UDP_MTU;

        pfms[npfms++] = pfm;

        int n = u24_encode(rid_, p);
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

        Segment seg;
        seg.cmd = RUX_CMD_ACK;

        AckQue::iterator itr;

        while (ack_que_.size()) {
            if (pfm && nleft < RUX_SEG_HDR_SIZE + RUX_FRM_EX_SIZE) {
                pfm->len = uint16_t(p - pfm->raw);

                pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
                ASSERT(pfm);
                uint8_t* p = pfm->raw;
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

            itr = ack_que_.begin();

            seg.sn = itr->first;
            seg.us = itr->second;
            n = seg.encode(p, nleft);
            ASSERT(n == RUX_SEG_HDR_SIZE);
            p += n;
            nleft -= n;

            ack_que_.erase(itr++);
        }

        pfm->len = uint16_t(p - pfm->raw);
        return npfms;
    }


    int _flush_psh(uint64_t now_us, Frame::ptr *pfms, int npfms) {
        int snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());
        if (snd_wnd == 0) {
            return 0;
        }

        int      nleft;
        int      nsave   = npfms;
        int      needsnd = 0;
        uint64_t una     = rcv_nxt_;

        int          n, i;
        uint8_t      wnd;
        Segment::ptr pseg;
        Frame::ptr   pfm;
        uint8_t* p = nullptr;

        if (nsave > 0) {
            pfm = pfms[nsave - 1];
            nleft = UDP_MTU - int(pfm->len);
            p = pfm->raw + pfm->len;
        }
        else {
            pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
            ASSERT(pfm);
            p = pfm->raw;
            nleft = UDP_MTU;

            n = u24_encode(rid_, p);
            p += n;
            nleft -= n;

            
            if (RUX_RWND_MAX > rcv_buf_.size()) {
                wnd = RUX_RWND_MAX - int(rcv_buf_.size());
            }
            else {
                wnd = 1;
            }

            n = u8_encode((uint8_t)wnd, p);
            p += n;
            nleft -= n;

            n = u48_encode(una, p);
            p += n;
            nleft -= n;
        }

        auto itr = snd_buf_.begin();
        while (npfms - nsave <= snd_wnd && npfms < RUX_RWND_MAX && itr != snd_buf_.end()) {
            pseg = itr->second;

            if (nleft < RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE + pseg->len) {
                pfm->len = uint16_t(p - pfm->raw);
                pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
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
            else if (pseg->xmit < RUX_XMIT_MAX) {
                if (pseg->resend_us <= now_us) {
                    DLOG("-------------------------------------------------------: SN[%lu], US[%lu] now: %lu, resnd_us: %lu\n", pseg->sn, pseg->us, now_us, pseg->resend_us);
                    pseg->rto *= 1.5;
                    needsnd = 1;
                }
                else if (pseg->fastack >= RUX_FAST_ACK) {
                    DLOG("########################################################\n");
                    pseg->fastack = 0;
                    pseg->rto = rto_;

                    needsnd = 1;
                }
                else if (pseg->sn == 0) {
                    break;
                }
            }
            else {
                DLOG("state changed, rux xmit limit\n");
                state_ = -1;

                if (pfm != pfms[npfms - 1]) {
                    pfms[npfms++] = pfm;
                }

                for (i = 0; i < npfms; i++) {
                    delete pfms[i];
                }

                return -1;
            }

            if (needsnd) {
                pseg->xmit++;
                pseg->us = now_us;
                pseg->resend_us = pseg->rto + now_us;
                n = pseg->encode(p, nleft);
                ASSERT(n == pseg->len + RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE);
                p += n;
                nleft -= n;
                needsnd = 0;
            }

            itr++;
        } // while (already_snd < snd_wnd && psh_itr != snd_buf_.end());

        pfm->len = uint16_t(p - pfm->raw);
        return npfms - nsave;
    }

    uint32_t    rid_;       // rux id
    uint64_t    base_us_;   // 启始时间(微秒)
    FrameQueue& snd_que_;   // io output queue 引用

    uint8_t  cwnd_      = RUX_SWND_MIN;
    uint8_t  ssthresh_  = RUX_SSTHRESH_INIT;
    uint8_t  rmt_wnd_   = RUX_SWND_MIN;
    
    int rto_    = RUX_RTO_MIN;   // RTO
    int srtt_   = 0;             // smooth RTT
    int rttval_ = 0;          
    
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
