#ifndef __XQ_NET_RUX__
#define __XQ_NET_RUX__


#include <map>
#include "xq/net/udx.hpp"


namespace xq {
namespace net {


/* rux property */
constexpr int     RUX_FRM_EX_SIZE     = 10;
constexpr int     RUX_SEG_HDR_SIZE    = 13;                                                                 // cmd[1] + sn[6] + us[6];
constexpr int     RUX_SEG_HDR_EX_SIZE = 3;                                                                  // len[2] + frg[1]
constexpr int     RUX_MSS             = UDP_MTU - RUX_FRM_EX_SIZE - RUX_SEG_HDR_SIZE - RUX_SEG_HDR_EX_SIZE; // RUX Maximum segment size
constexpr int     RUX_CMD_ACK         = 0x11;                                                               // CMD: ACK
constexpr int     RUX_CMD_PIN         = 0x12;                                                               // CMD: Heart's beat: Ping
constexpr int     RUX_CMD_CON         = 0x13;                                                               // CMD: Connection
constexpr int     RUX_CMD_PSH         = 0x14;                                                               // CMD: Push
constexpr int     RUX_RID_MAX         = 100000;                                                             // Maximum rux id
constexpr int     RUX_RWND_MAX        = 64;                                                                 // Maximum receive window size
constexpr int     RUX_SWND_MAX        = 3;                                                                  // Maximum send window size
constexpr int     RUX_SWND_MIN        = 1;                                                                  // Minimun send window size
constexpr int64_t RUX_SN_MAX          = 0x0000FFFFFFFFFFFF;                                                 // Maximum sequnce number
constexpr int64_t RUX_US_MAX          = 0x0000FFFFFFFFFFFF;                                                 // Maximum timestamp(us)
constexpr int     RUX_FRM_MAX         = 92;                                                                 // Maximum fragment size
constexpr int     RUX_MSG_MAX         = RUX_FRM_MAX * RUX_MSS;                                              // Maximum signle massage's length
constexpr int64_t RUX_RTO_MIN         = 500 * 1000;                                                         // RTO MIN 200ms
constexpr int64_t RUX_RTO_MAX         = 1000 * 1000 * 30;                                                   // RTO MAX 30s
constexpr int     RUX_TIMEOUT         = RUX_RTO_MAX * 2 * 10;                                               // TIMEOUT: 10 min
constexpr int     RUX_FAST_ACK        = 3;
constexpr int     RUX_XMIT_MAX        = 20;
constexpr int     RUX_SSTHRESH_INIT   = 2;


class Rux {
public:
    typedef Rux* ptr;
    typedef SpinLock LockType;


    /// @brief Decode rux header
    /// @param p       raw data
    /// @param datalen raw data's length
    /// @param rid     [out] rux id
    /// @param wnd     [out] remote window
    /// @param una     [out] remote receive una
    /// @return        return header' size on success or -1 on failure.
    static int decode_hdr(const uint8_t* data, socklen_t datalen, uint32_t* rid, uint8_t* wnd, int64_t* una) {
        ASSERT(data && datalen >= RUX_FRM_EX_SIZE);

        const uint8_t* p = data;
        p += u24_decode(p, rid);
        if (*rid == 0 || *rid > RUX_RID_MAX) {
            return -1;
        }

        p += u8_decode(p, wnd);
        if (*wnd == 0 || *wnd > RUX_RWND_MAX) {
            return -1;
        }

        p += s48_decode(p, una);
        if (*una > RUX_SN_MAX) {
            return -1;
        }

        return int(p - data);
    }


    static int encode_hdr(uint8_t* buf, socklen_t buflen, uint32_t rid, uint8_t wnd, int64_t una) {
        ASSERT(buf && buflen >= RUX_FRM_EX_SIZE);

        if (rid == 0 || rid > RUX_RID_MAX) {
            return -1;
        }

        if (wnd == 0 || wnd > RUX_RWND_MAX) {
            return -1;
        }

        if (una > RUX_SN_MAX) {
            return -1;
        }

        uint8_t* p = buf;
        p += u24_encode(rid, p);
        p += u8_encode(wnd, p);
        p += s48_encode(una, p);

        return int(p - buf);
    }


    /// @brief RUX Segment
    struct Segment {
        typedef Segment* ptr;

        uint8_t cmd = 0;             // command
        int64_t sn  = 0;             // sequence number
        int64_t us  = 0;             // timestamp(us)

        int16_t len = 0;             // segment's length
        uint8_t frg = 0;             // fragment
        uint8_t data[RUX_MSS] = {};  // data: 1424

        /* META */
        uint8_t fastack   = 0;       //
        int16_t xmit      = 0;       // resend times
        int32_t rto       = 0;       //
        int64_t resend_us = 0;       // next resend unix timestamp(us)


        /// @brief Default constructor
        Segment()
        {}


        /// @brief Constructor
        /// @param cmd RUX CMD
        /// @param sn  RUX SN
        /// @param len RUX Data's length
        /// @param frg RUX Fregment
        /// @param data data
        Segment(uint8_t cmd, int64_t sn, int16_t len, uint8_t frg, const uint8_t *data)
          : cmd(cmd)
          , sn(sn)
          , len(len)
          , frg(frg) {
            ASSERT(len <= RUX_MSS);
            ASSERT(data && len > 0 && len <= RUX_MSS);
            ::memcpy(this->data, data, len);
        }


        /// @brief Decode segment from buffer
        /// @param buf 
        /// @param buflen 
        /// @return decode size on success or -1 on failure
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

                p += s48_decode(p, &sn);
                if (sn > RUX_SN_MAX) {
                    break;
                }

                p += s48_decode(p, &us);
                if (us > RUX_US_MAX) {
                    break;
                }

                if (cmd == RUX_CMD_CON || cmd == RUX_CMD_PSH) {
                    if (buflen < RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE) {
                        break;
                    }

                    p += s16_decode(p, &len);
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


        /// @brief Encode segment into buffer
        /// @param buf     [in | out]
        /// @param buflen 
        /// @return encode size on success or -1 on failure
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
                p += s48_encode(sn, p);
                p += s48_encode(us, p);
                if (len > 0) {
                    p += s16_encode(len, p);
                    p += u8_encode(frg, p);
                    ::memcpy(p, data, len);
                    p += len;
                }

                ret = int(p - buf);
            } while (0);
            
            return ret;
        }
    }; // struct Segment;


    typedef std::map<int64_t, Segment::ptr> RcvBuf;
    typedef std::map<int64_t, Segment::ptr> SndBuf;
    typedef std::map<int64_t, int64_t>      AckQue;


    /// @brief Constructor
    /// @param rid RUX ID
    /// @param now_us current unix timestamp(us)
    /// @param snd_que Reference of FrameQueue for send.
    Rux(uint32_t rid, int64_t now_us, FrameQueue& snd_que)
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


    /// @brief set remote address
    void set_rmt_addr(const sockaddr_storage* addr, socklen_t addrlen) {
        lkr_.lock();
        _set_remote_addr(addr, addrlen);
        lkr_.unlock();
    }


    /// @brief rux worker queue
    int qid() const {
        return qid_;
    }


    /// @brief set worker queue
    void set_qid(int qid) {
        qid_ = qid;
    }


    /// @brief rux id 
    uint32_t rid() const {
        return rid_;
    }


    /// @brief State of rux.
    /// @note -1: invalid state. disconnected.
    ///        1: active  state  connected
    ///        0: init    state  init but do not send RUX_CMD_CON.
    int state() const {
        return state_;
    }


    /// @brief Set state
    /// @note  Developer(Coder) should not call this function.
    void set_state(int state) {
        ASSERT(state == 0 || state == -1 || state == 1);
        state_ = state;
    }


    /// @brief Smooth RTT
    int64_t srtt() const {
        return srtt_;
    }


    /// @brief  Retransmition
    uint64_t xmit() const {
        return xmit_;
    }


    /// @brief Rux input will process raw frame
    /// @param pfm Raw frame
    /// @return 0 on success or -1 on failure.
    int input(Frame::ptr pfm) {
        ASSERT(pfm && pfm->ex == this);

        int ret        = -1;
        int err        = 0;

        int datalen    = pfm->len;
        uint8_t* p     = pfm->raw;
        int64_t now_us = pfm->time_us - base_us_;

        // Step 1: Decode rux header
        uint32_t rid = 0;
        uint8_t  rmt_wnd = 0;
        int64_t  una = 0;

        int n = decode_hdr(p, datalen, &rid, &rmt_wnd, &una);
        if (n < 0) {
            return -1;
        }

        p += n;
        datalen -= n;

        int64_t sn, us;
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

                    auto ack_itr = snd_buf_.find(sn);
                    if (snd_buf_.end() != ack_itr) {
                        int64_t rtt = now_us - us + 1;
                        for (auto itr = snd_buf_.begin(); itr != ack_itr; ++itr) {
                            itr->second->fastack++;
                        }

                        delete ack_itr->second;
                        snd_buf_.erase(ack_itr);

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
                                srtt_ = (7 * srtt_ + rtt) >> 3;
                            }

                            rto_ = MID(RUX_RTO_MIN, srtt_ + rttval_ * 4, RUX_RTO_MAX);
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
            last_rcv_us_ = pfm->time_us;
            _set_remote_addr(&pfm->name, pfm->namelen);
            ret = 0;
        } while (0);
        lkr_.unlock();

        return ret;
    }


    /// @brief Receive data into msg
    /// @param msg [out]
    /// @return The number of message on success or -1 on failure.
    int recv(uint8_t* msg) {
        int      n  = 0;
        int      ok = 0;
        uint8_t* p  = msg;

        int     i;
        int16_t len;

        Segment::ptr pseg;
        Segment::ptr psegs[RUX_FRM_MAX];

        RcvBuf::iterator itr;

        lkr_.lock();
        int64_t nxt = rcv_nxt_;
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


    /// @brief send data
    /// @param msg 
    /// @param msglen 
    /// @return 0 on success or -1 on failure
    int send(const uint8_t* msg, int msglen) {
        ASSERT(msg && msglen <= RUX_MSG_MAX);

        uint8_t cmd;        
        int len, i, n, ret = -1;
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


    /// @brief Pacing to update
    /// @param now_us current unix timestamp(us)
    /// @return 0 on success or -1 on failure.
    int update(int64_t now_us) {
        int snd_wnd, i;
        int n       = 0;
        int npfms   = 0;
        int needsnd = 0;
        int err     = 0;
        int ret     = -1;

        uint8_t wnd;
        int16_t nleft;
        size_t  nrcv;

        AckQue::iterator ack_itr;
        SndBuf::iterator psh_itr;
        Segment::ptr pseg;
        
        uint8_t* p;
        Frame::ptr pfm = nullptr;
        Frame::ptr pfms[RUX_RWND_MAX];

        lkr_.lock();

        do {
            if (now_us > last_rcv_us_ && last_rcv_us_ > 0 && now_us > last_rcv_us_ && now_us - last_rcv_us_ > RUX_TIMEOUT) {
                state_ = -1;
            }

            if (state_ == -1) {
                break;
            }

            now_us -= base_us_;

            if (ack_que_.size()) {
                Segment seg;
                seg.cmd = RUX_CMD_ACK;

                if (!pfm) {
                    pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
                    ASSERT(pfm);

                    p       = pfm->raw;
                    nleft   = sizeof(pfm->raw);
                    nrcv    = rcv_buf_.size();
                    wnd     = RUX_RWND_MAX > nrcv ? uint8_t(RUX_RWND_MAX - (int)nrcv) : 1;

                    n = encode_hdr(p, nleft, rid_, wnd, rcv_nxt_);
                    ASSERT(n == RUX_FRM_EX_SIZE);
                    p += n;
                    nleft -= n;
                }

                do {
                    if (nleft < RUX_SEG_HDR_SIZE + RUX_FRM_EX_SIZE) {
                        pfm->len = int16_t(p - pfm->raw);

                        pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
                        ASSERT(pfm);

                        p       = pfm->raw;
                        nleft   = sizeof(pfm->raw);
                        nrcv    = rcv_buf_.size();
                        wnd     = RUX_RWND_MAX > nrcv ? uint8_t(RUX_RWND_MAX - (int)nrcv) : 1;

                        n = encode_hdr(p, nleft, rid_, wnd, rcv_nxt_);
                        ASSERT(n == RUX_FRM_EX_SIZE);
                        p += n;
                        nleft -= n;
                    }

                    ack_itr = ack_que_.begin();

                    seg.sn = ack_itr->first;
                    seg.us = ack_itr->second;
                    n = seg.encode(p, nleft);
                    ASSERT(n == RUX_SEG_HDR_SIZE);
                    p += n;
                    nleft -= n;

                    ack_que_.erase(ack_itr++);
                } while (ack_que_.size());
            }

            snd_wnd = MIN3(rmt_wnd_, cwnd_, (int)snd_buf_.size());

            if (snd_wnd && !pfm) {
                pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
                ASSERT(pfm);

                p       = pfm->raw;
                nleft   = sizeof(pfm->raw);
                nrcv    = rcv_buf_.size();
                wnd     = RUX_RWND_MAX > nrcv ? uint8_t(RUX_RWND_MAX - (int)nrcv) : 1;

                n = encode_hdr(p, nleft, rid_, wnd, rcv_nxt_);
                ASSERT(n == RUX_FRM_EX_SIZE);
                p += n;
                nleft -= n;
            }

            psh_itr = snd_buf_.begin();
            while (npfms <= snd_wnd && npfms < RUX_RWND_MAX && psh_itr != snd_buf_.end()) {
                ASSERT(pfm);
                pseg = psh_itr->second;

                if (nleft < RUX_SEG_HDR_EX_SIZE + RUX_SEG_HDR_SIZE + (int)pseg->len) {
                    pfm->len = int16_t(p - pfm->raw);

                    if (npfms == snd_wnd) {
                        break;
                    }

                    pfms[npfms++] = pfm = new Frame(&addr_, addrlen_);
                    ASSERT(pfm);

                    p       = pfm->raw;
                    nleft   = sizeof(pfm->raw);
                    nrcv    = rcv_buf_.size();
                    wnd     = RUX_RWND_MAX > nrcv ? uint8_t(RUX_RWND_MAX - (int)nrcv) : 1;

                    n = encode_hdr(p, nleft, rid_, wnd, rcv_nxt_);
                    ASSERT(n == RUX_FRM_EX_SIZE);
                    p += n;
                    nleft -= n;
                }

                if (pseg->xmit == 0) {
                    pseg->rto = rto_;
                    needsnd = 1;
                }
                else if (pseg->xmit < RUX_XMIT_MAX) {
                    if (pseg->resend_us <= now_us) {
                        DLOG("----- SN[%lu] resend\n", pseg->sn);
                        pseg->rto *= 1.5;

                        needsnd = 1;
                        xmit_++;
                    }
                    else if (pseg->fastack >= RUX_FAST_ACK) {
                        DLOG("+++++ SN[%lu] fastack\n", pseg->sn);

                        pseg->fastack = 0;
                        pseg->rto = rto_;

                        needsnd = 1;
                        xmit_++;
                    }
                    else if (pseg->sn == 0) {
                        break;
                    }
                }
                else {
                    state_ = -1;
                    for (i = 0; i < npfms; i++) {
                        delete pfms[i];
                    }

                    err = 1;
                    break;
                }

                if (needsnd) {
                    pseg->xmit++;
                    pseg->us        = now_us;
                    pseg->resend_us = pseg->rto + now_us;

                    n = pseg->encode(p, nleft);
                    ASSERT(n == pseg->len + RUX_SEG_HDR_SIZE + RUX_SEG_HDR_EX_SIZE);
                    p += n;
                    nleft -= n;

                    needsnd = 0;
                }

                psh_itr++;
            }

            if (err) {
                break;
            }

            ret = 0;
        } while (0);

        lkr_.unlock();

        if (ret == 0 && npfms > 0) {
            pfm->len = int16_t(p - pfm->raw);
            ASSERT(snd_que_.enqueue_bulk(pfms, npfms));
        }

        return ret;
    }


private:
    void _reset(int64_t now_us) {
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
        
        base_us_     = now_us;
        cwnd_        = RUX_SWND_MIN;
        ssthresh_    = RUX_SSTHRESH_INIT;
        rmt_wnd_     = RUX_SWND_MIN;
        xmit_        = 0;
        rto_         = RUX_RTO_MIN;
        srtt_        = 0;
        rttval_      = 0;
        rcv_nxt_     = 0;
        snd_nxt_     = 0;
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

    /* rux basic */
    uint32_t         rid_;                              // rux id
    int64_t          base_us_;                          // 启始时间(微秒)
    int64_t          rcv_nxt_     = 0;                  // 下一次接收 sn
    int64_t          snd_nxt_     = 0;                  // 下一次发送 sn
    int64_t          last_rcv_us_ = 0;                  // 最后一次接收时间
    uint64_t         xmit_        = 0;
    /* congestion control */
    uint8_t          cwnd_        = RUX_SWND_MIN;
    uint8_t          ssthresh_    = RUX_SSTHRESH_INIT;
    uint8_t          rmt_wnd_     = RUX_SWND_MIN;
    /* RTO SRTT */
    int64_t          rto_         = RUX_RTO_MIN;        // RTO
    int64_t          srtt_        = 0;                  // smooth RTT
    int64_t          rttval_      = 0;          
    /* IO */
    socklen_t        addrlen_     = sizeof(sockaddr_storage);
    sockaddr_storage addr_        = {};
    FrameQueue&      snd_que_;                          // io output queue 引用
    std::atomic<int> state_       = -1;
    std::atomic<int> qid_         = -1;  // rux_que index

    LockType lkr_;
    AckQue   ack_que_;             // ACK队列
    SndBuf   snd_buf_;             // 发送缓冲区
    RcvBuf   rcv_buf_;             // 接收缓冲区
}; // class Rux;


} // namespace xq;
} // namespace net;


#endif // !__XQ_NET_RUX__
