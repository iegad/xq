#ifndef __XQ_NET_XDG__
#define __XQ_NET_XDG__


#include <deque>
#include <unordered_set>
#include "xq/net/udp_session.hpp"


namespace xq {
namespace net {


/* -------------------------------------------------------------- */
/// @brief UDX Max transmit uint, UDX最大传输单元
///
constexpr size_t UDX_MTU = UDP_DGX_SIZE;

/* -------------------------------------------------------------- */
/// @brief UDX uid length, UDX uid长度 3 bytes
///
constexpr size_t UDX_UID_LEN = 3;

/* -------------------------------------------------------------- */
/// @brief UDX header length, UDX消息头长度: CMD[8b] + WND[8b]
///
constexpr size_t UDX_HDR_LEN = 14;

/* -------------------------------------------------------------- */
/// @brief UDX header length, UDX消息头长度: CMD[8b] + WND[8b]
///
constexpr size_t   UDX_ACK_LEN = UDX_HDR_LEN + 1;
constexpr size_t   UDX_PSH_MIN = UDX_HDR_LEN + 3;
constexpr size_t   UDX_MSS = (UDX_MTU - UDX_UID_LEN - UDX_PSH_MIN) / 16 * 16;
constexpr size_t   UDX_PSH_MAX = UDX_PSH_MIN + UDX_MSS;
constexpr uint8_t  UDX_FRG_MAX = 256 * 1024 / UDX_MSS;
constexpr size_t   UDX_MSG_MAX = UDX_FRG_MAX * UDX_MSS;
constexpr uint8_t  UDX_CMD_PIN = 0x11;
constexpr uint8_t  UDX_CMD_PON = 0x12;
constexpr uint8_t  UDX_CMD_ACK = 0x13;
constexpr uint8_t  UDX_CMD_PSH = 0x14;
constexpr uint8_t  UDX_CMD_CON = 0x15;
constexpr uint8_t  UDX_RWN_MIN = 1;
constexpr uint8_t  UDX_RWN_MAX = 128;
constexpr uint8_t  UDX_SWN_MAX = UDX_RWN_MAX / 4;
constexpr uint8_t  UDX_STH_INI = 12;
constexpr uint32_t UDX_UID_MAX = 100000;
constexpr uint64_t UDX_SN_MAX  = 0x0000FFFFFFFFFFFF;
constexpr uint64_t UDX_TS_MAX  = 0x0000FFFFFFFFFFFF;
constexpr uint32_t UDX_RTO_MIN = 100;
constexpr uint32_t UDX_RTO_MAX = 60000;
constexpr uint64_t UDX_UPD_INT = 0;
constexpr uint32_t UDX_RXM_MAX = 20; // 最大重传次数


class Udx {
public:
    typedef std::pair<uint64_t, uint64_t> Ack;
    typedef std::shared_ptr<Udx> Ptr;


    static __inline__ int _u48_decode(const uint8_t* p, uint64_t* v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        uint8_t* tmp = (uint8_t*)v;
        *(tmp + 7) = *p;
        *(tmp + 6) = *(p + 1);
        *(tmp + 5) = *(p + 2);
        *(tmp + 4) = *(p + 3);
        *(tmp + 3) = *(p + 4);
        *(tmp + 2) = *(p + 5);
#else
        ::memcpy(v, p, 6);
#endif
        return 6;
    }


    static __inline__ int _u48_encode(uint64_t v, uint8_t* p) {
        uint8_t* tmp = (uint8_t*)&v; // 00 00 01 02 03 04 05 06 =>
#if X_BIG_ENDIAN || X_MUST_ALIGN
        * p = *(tmp + 7);
        *(p + 1) = *(tmp + 6);
        *(p + 2) = *(tmp + 5);
        *(p + 3) = *(tmp + 4);
        *(p + 4) = *(tmp + 3);
        *(p + 5) = *(tmp + 2);
#else
        ::memcpy(p, tmp, 6);
#endif
        return 6;
    }


    static __inline__ int _u24_decode(const uint8_t* p, uint32_t* v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        uint8_t* tmp = (uint8_t*)v;
        *(tmp + 3) = *p;
        *(tmp + 2) = *(p + 1);
        *(tmp + 1) = *(p + 2);
#else
        ::memcpy(v, p, 3);
#endif
        return 3;
    }


    static __inline__ int _u24_encode(uint32_t v, uint8_t* p) {
        uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
        * p = *(tmp + 3);
        *(p + 1) = *(tmp + 2);
        *(p + 2) = *(tmp + 1);
#else
        ::memcpy(p, tmp, 3);
#endif
        return 3;
    }


    static __inline__ int _u16_decode(const uint8_t* p, uint16_t* v) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        uint8_t* tmp = (uint8_t*)v;
        *(tmp + 1) = *p;
        *tmp = *(p + 1);
#else
        ::memcpy(v, p, 2);
#endif
        return 2;
    }


    static int _u16_encode(uint16_t v, uint8_t* p) {
        uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
        * p = *(tmp + 1);
        *(p + 1) = *tmp;
#else
        ::memcpy(p, tmp, 2);
#endif
        return 2;
    }


    static __inline__ int _u8_decode(const uint8_t* p, uint8_t* v) {
        *v = *p;
        return 1;
    }


    static __inline__ int _u8_encode(uint8_t v, uint8_t* p) {
        *p = v;
        return 1;
    }


public:
    // ------------------------------------------------------------------ BEG Segment ------------------------------------------------------------------
    struct Segment {
        uint8_t  cmd;
        uint8_t  wnd;
        uint64_t sn;
        uint64_t ts;

        // META
        uint8_t fastack;
        uint8_t xmit;
        uint64_t resend_ts;

        union {
            uint8_t acc;
            struct {
                uint8_t  frg;
                uint16_t len;
                uint8_t  data[UDX_MSS];
            };
        };


        ~Segment() = default;
        Segment(const Segment&) = delete;
        Segment(const Segment&&) = delete;
        Segment operator=(const Segment&) = delete;
        Segment operator=(const Segment&&) = delete;


        Segment() {
            ::memset(this, 0, sizeof(Segment));
        }


        std::string to_string() {
            char buf[UDX_MSS * 2 + 80];
            int n = 0;
            switch (cmd) {
            case UDX_CMD_PIN:
            case UDX_CMD_PON: n = sprintf(buf, "[CMD:0x%x][WND:%3d][SN:%llu][TS:%llu]", 
                cmd, wnd, sn, ts); 
                break;

            case UDX_CMD_ACK: n = sprintf(buf, "[CMD:0x%x][WND:%3d][SN:%llu][TS:%llu][ACC:%d]", 
                cmd, wnd, sn, ts, acc); 
                break;

            case UDX_CMD_PSH:
            case UDX_CMD_CON: n = sprintf(buf, "[CMD:0x%x][WND:%3d][SN:%llu][TS:%llu][FRG:%3d][LEN:%d] %s", 
                cmd, wnd, sn, ts, frg, len, xq::tools::bin2hex(data, len).c_str()); 
                break;

            default: break;
            }
            return std::string(buf, n);
        }


        Segment(uint8_t cmd, uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t acc, uint8_t frg, const uint8_t *data, size_t datalen)
            : cmd(cmd)
            , wnd(rcv_wnd)
            , sn(sn)
            , ts(ts)
            , fastack(0)
            , xmit(0)
            , resend_ts(0)
            , acc(cmd == UDX_CMD_ACK ? acc : frg)
            , frg(cmd == UDX_CMD_ACK ? acc : frg)
            , len(datalen) {
                
            if (cmd >= UDX_CMD_PSH && data && datalen > 0){
                ::memcpy(this->data, data, datalen);
                return;
            }
            ::memset(this->data, 0, UDX_MSS);
        }


        static __inline__ Segment* new_pin(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            return new Segment(UDX_CMD_PIN, rcv_wnd, sn, ts, 0, 0, nullptr, 0);
        }


        static __inline__ Segment* new_pon(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            return new Segment(UDX_CMD_PON, rcv_wnd, sn, ts, 0, 0, nullptr, 0);
        }


        static __inline__ Segment* new_ack(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t acc) {
            return new Segment(UDX_CMD_ACK, rcv_wnd, sn, ts, acc, 0, nullptr, 0);
        }


        static __inline__ Segment* new_psh(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t frg, const uint8_t* data, size_t datalen) {
            return new Segment(UDX_CMD_PSH, rcv_wnd, sn, ts, 0, frg, data, datalen);
        }


        static __inline__ Segment* new_con(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t frg, const uint8_t* data, size_t datalen) {
            return new Segment(UDX_CMD_CON, rcv_wnd, sn, ts, 0, frg, data, datalen);
        }


        static __inline__ int decode(Segment** seg, const uint8_t* buf, size_t buflen) {
            if (!buf) {
                return -1;
            }

            if (buflen < UDX_HDR_LEN) {
                return -2;
            }

            const uint8_t* p = buf;
            uint8_t cmd, wnd, acc = 0, frg = 0;
            uint16_t len = 0;
            uint64_t sn = 0, ts = 0;

            p += Udx::_u8_decode(p, &cmd);
            if (cmd < UDX_CMD_PIN || cmd > UDX_CMD_CON) {
                return -3;
            }
        
            p += Udx::_u8_decode(p, &wnd);
            if (wnd > UDX_RWN_MAX) {
                return -4;
            }

            p += Udx::_u48_decode(p, &sn);
            if (sn > UDX_SN_MAX) {
                return -5;
            }

            p += Udx::_u48_decode(p, &ts);
            if (ts > UDX_TS_MAX) {
                return -6;
            }

            if (cmd == UDX_CMD_ACK) {
                if (buflen < UDX_ACK_LEN) {
                    return -7;
                }

                p += Udx::_u8_decode(p, &acc);
                if (acc > 1) {
                    return -8;
                }
            }
            else if (cmd >= UDX_CMD_PSH) {
                if (cmd == UDX_CMD_CON && sn != 0) {
                    return -9;
                }

                if (buflen < UDX_PSH_MIN) {
                    return -10;
                }

                p += Udx::_u8_decode(p, &frg);
                if (frg > UDX_FRG_MAX) {
                    return -11;
                }

                p += Udx::_u16_decode(p, &len);
                if (len > UDX_MSS) {
                    return -12;
                }

                if (buflen < len + UDX_PSH_MIN) {
                    return -13;
                }

                if (len > 0) {
                    p += len;
                }
            }

            switch (cmd) {
            case UDX_CMD_PIN: *seg = Segment::new_pin(wnd, sn, ts); break;
            case UDX_CMD_PON: *seg = Segment::new_pon(wnd, sn, ts); break;
            case UDX_CMD_ACK: *seg = Segment::new_ack(wnd, sn, ts, acc); break;
            case UDX_CMD_CON: *seg = Segment::new_con(wnd, sn, ts, frg, p - len, len); break;
            case UDX_CMD_PSH: *seg = Segment::new_psh(wnd, sn, ts, frg, p - len, len); break;
            }
            return p - buf;
        }


        __inline__ int encode(uint8_t *buf, size_t buflen) {
            ASSERT(buf);

            if (buflen < UDX_HDR_LEN) {
                return -1;
            }

            if (wnd > UDX_RWN_MAX) {
                return -2;
            }

            if (sn > UDX_SN_MAX) {
                return -3;
            }

            if (ts > UDX_TS_MAX) {
                return -4;
            }

            uint8_t* p = buf;

            switch (cmd) {
            case UDX_CMD_PIN:
            case UDX_CMD_PON: {
                if (buflen < UDX_HDR_LEN) {
                    return -5;
                }

                p += Udx::_u8_encode(cmd, p);
                p += Udx::_u8_encode(wnd, p);
                p += Udx::_u48_encode(sn, p);
                p += Udx::_u48_encode(ts, p);
            } break;

            case UDX_CMD_ACK: {
                if (buflen < UDX_ACK_LEN) {
                    return -5;
                }

                p += Udx::_u8_encode(cmd, p);
                p += Udx::_u8_encode(wnd, p);
                p += Udx::_u48_encode(sn, p);
                p += Udx::_u48_encode(ts, p);
                p += Udx::_u8_encode(acc, p);
            } break;

            case UDX_CMD_CON:
            case UDX_CMD_PSH: {
                if (buflen < UDX_PSH_MIN + len) {
                    return -5;
                }

                p += Udx::_u8_encode(cmd, p);
                p += Udx::_u8_encode(wnd, p);
                p += Udx::_u48_encode(sn, p);
                p += Udx::_u48_encode(ts, p);
                p += Udx::_u8_encode(frg, p);
                p += Udx::_u16_encode(len, p);
                if (len > 0) {
                    ::memcpy(p, data, len);
                    p += len;
                }
            } break;

            default: return -5;
            }

            return p - buf;
        }
    };
    // ------------------------------------------------------------------ END Segment ------------------------------------------------------------------


    static Ptr create(uint32_t uid, UdpSession::Ptr &sess) {
        return Ptr(new Udx(uid, sess));
    }


    ~Udx() {
        snd_que_.clear();
        snd_buf_.clear();
        rcv_buf_.clear();
        ack_que_.clear();
    }


    void set_addr(const sockaddr *addr, socklen_t addrlen) {
        if (::memcmp(addr, &addr_, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
        }

        if (addrlen != addrlen_) {
            addrlen_ = addrlen;
        }
    }


    void connect(const std::string& remote) {
        ASSERT(xq::net::str2addr(remote, &addr_, &addrlen_));
    }


    int input(const uint8_t* buf, size_t buflen, const sockaddr* addr, socklen_t addrlen, int64_t now_ms) {
        ASSERT(buf && now_ms > 0);

        if (buflen < UDX_HDR_LEN + UDX_UID_LEN) {
            return -1;
        }

        const uint8_t* p = buf;
        uint32_t uid = 0;

        int n = Udx::_u24_decode(p, &uid);
        if (uid != uid_) {
            return -2;
        }
        p += n;
        buflen -= n;

        int now_ts = now_ms - start_ms_, res = 0;
        Segment* seg;

        while (buflen > 0) {
            n = Segment::decode(&seg, p, buflen);
            if (n < 0) {
                return -3;
            }

            ASSERT(seg);
            std::printf("%s\n", seg->to_string().c_str());
            if (seg->sn > last_sn_ || last_sn_ == 0) {
                rmt_wnd_ = seg->wnd;
                last_sn_ = seg->sn;

                // 拥塞控制
                if (cwnd_ < ssthresh_) {
                    cwnd_ <<= 1;
                }
                else {
                    cwnd_++;
                }

                if (cwnd_ > UDX_SWN_MAX) {
                    cwnd_ = UDX_SWN_MAX;
                }
            }

            switch (seg->cmd) {
            case UDX_CMD_ACK: 
                res = _update_ack(seg, now_ts); 
                break;

            case UDX_CMD_CON: 
                res = _update_con(now_ms, seg, addr, addrlen); 
                break;

            case UDX_CMD_PSH: 
                res = _update_psh(seg); 
                break;

            default: break;
            }

            if (res) {
                break;
            }

            p += n;
            buflen -= n;
            set_addr(addr, addrlen);
        }

        return res;
    }


    int recv(uint8_t* buf, size_t buflen) {
        ASSERT(buf && buflen > 0);

        bool ok = false;
        Segment* segs[UDX_FRG_MAX];
        size_t nsegs = 0;

        uint8_t prv_frg = 0xFF;
        uint64_t nxt = rcv_nxt_;
        auto itr = rcv_buf_.begin();

        for (; itr != rcv_buf_.end(); ++itr) {
            auto seg = itr->second;
            if (seg->sn != nxt) {
                return 0;
            }

            nxt++;
            segs[nsegs++] = seg;
            if (seg->frg == 0) {
                ok = true;
                itr++;
                break;
            }
        }

        if (ok) {
            rcv_nxt_ = nxt;
            rcv_buf_.erase(rcv_buf_.begin(), itr);

            uint8_t *p = buf;
            for (size_t i = 0; i < nsegs; i++) {
                Segment* seg = segs[i];
                ::memcpy(p, seg->data, seg->len);
                p += seg->len;
                delete seg;
            }

            return p - buf;
        }

        return 0;
    }


    int send(const uint8_t* data, size_t datalen) {
        ASSERT(data && datalen > 0 && datalen <= UDX_MSG_MAX);

        int n = datalen > UDX_MSS ? (datalen + UDX_MSS - 1) / UDX_MSS : 1;
        Segment* seg;

        for (int i = 1; i <= n; i++) {
            uint16_t len = i == n ? datalen : UDX_MSS;
            seg = snd_nxt_ == 0 && rcv_nxt_ == 0 && snd_buf_.empty() && snd_que_.empty() ?
                Segment::new_con(0, snd_nxt_++, 0, n - i, data, len) :
                Segment::new_psh(0, snd_nxt_++, 0, n - i, data, len);

            snd_que_.emplace_back(seg);
            data += len;
            datalen -= len;
        }

        return 0;
    }


    int flush(int64_t now_ms) {
        UdpSession::Datagram* dg = UdpSession::Datagram::get(nullptr, &addr_, addrlen_);

        uint64_t now_ts = now_ms - start_ms_;
        uint8_t* p = dg->data + UDX_UID_LEN;
        int nleft = UDX_MTU - UDX_UID_LEN, n;

        if (!ack_que_.empty()) {
            uint64_t cur_ts = now_ms - start_ms_;
            uint8_t acc_flag = 1;
            Segment seg;

            for (auto& ack : ack_que_) {
                if (nleft < UDX_ACK_LEN) {
                    dg->datalen = UDX_MTU - nleft;
                    _u24_encode(uid_, dg->data);
                    sess_->send(dg);

                    dg = UdpSession::Datagram::get(nullptr, &addr_, addrlen_);
                    p = dg->data + UDX_UID_LEN;
                    nleft = UDX_MTU - UDX_UID_LEN;
                }

                seg.cmd = UDX_CMD_ACK;
                seg.wnd = rcv_buf_.size() < UDX_RWN_MAX ? UDX_RWN_MAX - rcv_buf_.size() : 0;
                seg.sn = ack.first;
                seg.ts = ack.second;
                seg.acc = acc_flag;
                n = seg.encode(p, nleft);
                p += n;
                nleft -= n;
                if (acc_flag) {
                    acc_flag = 0;
                }
            }

            ack_que_.clear();
        }

        while (!snd_que_.empty()) {
            Segment* seg = snd_que_.front();
            seg->resend_ts = now_ts + rto_;
            seg->wnd = UDX_SWN_MAX - rcv_buf_.size();
            snd_buf_.insert(std::make_pair(seg->sn, seg));
            snd_que_.pop_front();
        }

        int needsend = 0, i = 0;
        uint8_t snd_wnd = xq::tools::MIN(cwnd_, rmt_wnd_);

        if (snd_wnd > 0) {
            for (auto& itr : snd_buf_) {
                Segment* seg = itr.second;

                if (i > snd_wnd) {
                    break;
                }

                if (nleft < seg->len + UDX_PSH_MIN) {
                    dg->datalen = UDX_MTU - nleft;
                    _u24_encode(uid_, dg->data);
                    sess_->send(dg);
                    dg = UdpSession::Datagram::get(nullptr, &addr_, addrlen_);
                    p = dg->data + UDX_UID_LEN;
                    nleft = UDX_MTU - UDX_UID_LEN;
                }

                if (seg->xmit == 0) {
                    // 第一次发送
                    needsend = 1;
                }
                else if (seg->xmit >= UDX_RXM_MAX) {
                    // 超过最大重传次数
                    return -1;
                }
                else if (seg->resend_ts <= now_ts) {
                    // 超时重传
                    needsend = 1;
                    seg->resend_ts += xq::tools::MID(UDX_RTO_MIN, (uint32_t)(rto_ * 1.3), UDX_RTO_MAX);

                    // TODO: 重新计算CWND大小
                    snd_wnd = xq::tools::MIN(cwnd_, rmt_wnd_);
                }

                if (needsend) {
                    seg->ts = now_ts;
                    seg->wnd = rcv_buf_.size() < UDX_RWN_MAX ? UDX_RWN_MAX - rcv_buf_.size() : 0;
                    n = seg->encode(p, nleft);
                    p += n;
                    nleft -= n;
                    seg->xmit++;
                    needsend = 0;
                    i++;
                }
            }
        }

        dg->datalen = UDX_MTU - nleft;
        ASSERT(dg->datalen == 3 || dg->datalen >= UDX_UID_LEN + UDX_HDR_LEN);
        if (dg->datalen <= 3) {
            delete dg;
            return 0;
        }

        _u24_encode(uid_, dg->data);
        sess_->send(dg);
        ASSERT(!sess_->flush());

        return 0;
    }


private:
    Udx(uint32_t uid, UdpSession::Ptr &sess)
        : sess_(sess)
        , addr_({0,{0}})
        , addrlen_(sizeof(addr_))
        , cwnd_(UDX_RWN_MIN)
        , ssthresh_(UDX_STH_INI)
        , rmt_wnd_(UDX_RWN_MIN)
        , uid_(uid)
        , srtt_(0)
        , rttvar_(0)
        , rto_(UDX_RTO_MIN)
        , start_ms_(0)
        , last_sn_(0)
        , rcv_nxt_(0)
        , snd_nxt_(0) 
    {}


    void _reset(int64_t now_ms) {
        cwnd_ = UDX_RWN_MIN;
        start_ms_ = now_ms;
        rto_ = UDX_RTO_MIN;
        snd_nxt_ = rcv_nxt_ = last_sn_ = srtt_ = rmt_wnd_ = 0;

        if (rcv_buf_.size() > 0) {
            for (auto& itr : rcv_buf_) {
                delete itr.second;
            }
            rcv_buf_.clear();
        }
        
        if (snd_que_.size() > 0) {
            for (auto& itr : snd_que_) {
                delete itr;
            }
            snd_que_.clear();
        }

        if (snd_buf_.size() > 0) {
            for (auto& itr : snd_buf_) {
                delete itr.second;
            }
            snd_buf_.clear();
        }
    }


    __inline__ int _update_con(int64_t now_ms, Segment* new_seg, const sockaddr *addr, socklen_t addrlen) {
        if (rcv_nxt_ != new_seg->sn && 0 && addrlen != addrlen_ && ::memcmp(addr, &addr_, addrlen)) {
            return -1;
        }

        this->_reset(now_ms);
        return this->_update_psh(new_seg);
    }


    __inline__ int _update_psh(Segment* new_seg) {
        if (new_seg->sn >= rcv_nxt_ + UDX_RWN_MAX) {
            delete new_seg;
            return -1;
        }

        auto ack_itr = ack_que_.find(new_seg->sn - 1);
        if (ack_itr != ack_que_.end()) {
            ack_que_.erase(ack_itr);
        }

        ack_itr = ack_que_.find(new_seg->sn + 1);
        if (ack_itr == ack_que_.end()) {
            ack_que_.insert(std::make_pair(new_seg->sn, new_seg->ts));
        }
        
        if (new_seg->sn < rcv_nxt_) {
            delete new_seg;
            return 0;
        }

        auto seg_itr = rcv_buf_.find(new_seg->sn);
        if (seg_itr != rcv_buf_.end()) {
            delete new_seg;
            return 0;
        }

        rcv_buf_.insert(std::make_pair(new_seg->sn, new_seg));
        return 0;
    }


    __inline__ int _update_ack(Segment* seg, uint64_t now_ts) {
        auto itr = snd_buf_.find(seg->sn);
        if (itr != snd_buf_.end()) {
            if (seg->acc) {
                snd_buf_.erase(snd_buf_.begin(), itr);
            }
            snd_buf_.erase(itr);
        }

        uint64_t rtt = now_ts - seg->ts;
        if (rtt < 0) {
            delete seg;
            return -1;
        }

        if (srtt_ == 0) {
            srtt_ = rtt;
            rttvar_ = rtt / 2;
        }
        else {
            uint64_t delta = rtt - srtt_;
            if (delta < 0) {
                delta = -delta;
            }

            rttvar_ = (3 * rttvar_ + delta) / 4;
            srtt_ = (7 * srtt_ + rtt) / 8;

            if (srtt_ < 1) {
                srtt_ = 1;
            }
        }

        uint32_t tmp = srtt_ + xq::tools::MAX(UDX_UPD_INT, 4 * rttvar_);
        rto_ = xq::tools::MID(UDX_RTO_MIN, tmp, UDX_RTO_MAX);

        delete seg;
        return 0;
    }


    UdpSession::Ptr sess_;
    sockaddr addr_;
    socklen_t addrlen_;

    uint8_t  cwnd_, rmt_wnd_, ssthresh_;
    uint64_t uid_, srtt_, rttvar_, rto_;
    uint64_t start_ms_, last_sn_;
    uint64_t rcv_nxt_, snd_nxt_;

    std::map<uint64_t, uint64_t> ack_que_;
    std::map<uint64_t, Segment*> rcv_buf_;
    std::deque<Segment*> snd_que_;
    std::map<uint64_t, Segment*> snd_buf_;
}; // class Xdg;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_XDG__
