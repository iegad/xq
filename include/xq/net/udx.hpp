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
constexpr int UDX_MTU = UDP_DGX_SIZE;

/* -------------------------------------------------------------- */
/// @brief UDX uid length, UDX uid长度 3 bytes
///
constexpr int UDX_UID_LEN = 3;

/* -------------------------------------------------------------- */
/// @brief UDX header length, UDX消息头长度: CMD[8b] + WND[8b]
///
constexpr int UDX_HDR_LEN = 14;

/* -------------------------------------------------------------- */
/// @brief UDX header length, UDX消息头长度: CMD[8b] + WND[8b]
///
constexpr int UDX_ACK_LEN = UDX_HDR_LEN + 1;
constexpr int UDX_PSH_MIN = UDX_HDR_LEN + 3;
constexpr int UDX_MSS = (UDX_MTU - UDX_UID_LEN - UDX_PSH_MIN) / 16 * 16;
constexpr int UDX_PSH_MAX = UDX_PSH_MIN + UDX_MSS;
constexpr int UDX_FRG_MAX = 256 * 1024 / UDX_MSS;
constexpr int UDX_MSG_MAX = UDX_FRG_MAX * UDX_MSS;
constexpr int UDX_CMD_PIN = 0x11;
constexpr int UDX_CMD_PON = 0x12;
constexpr int UDX_CMD_ACK = 0x13;
constexpr int UDX_CMD_PSH = 0x14;
constexpr int UDX_CMD_CON = 0x15;
constexpr int UDX_UID_MAX = 100000;
constexpr int UDX_RTO_MIN = 100;
constexpr int UDX_RTO_MAX = 60000;
constexpr int UDX_UPD_INT = 0;
constexpr int UDX_RXMIT_MAX = 20; // 最大重传次数
constexpr int UDX_FAS_MAX = 3; // 快速重传

constexpr int UDX_RWND_MIN = 2;
constexpr int UDX_RWND_MAX = 128;
constexpr int UDX_SWND_MAX = UDX_RWND_MAX / 2;
constexpr int UDX_SSTHRESH_INIT = 8;

constexpr uint64_t UDX_SN_MAX = 0x0000FFFFFFFFFFFF;
constexpr uint64_t UDX_TS_MAX = 0x0000FFFFFFFFFFFF;


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

    // ------------------------------------------------------------------ BEG Segment ------------------------------------------------------------------
    struct Segment {
        uint8_t  cmd;
        uint8_t  wnd;
        uint64_t sn;
        uint64_t ts;

        // META
        uint8_t  fastack;
        uint8_t  xmit;
        uint64_t resend_ts;

        uint8_t  acc;
        uint8_t  frg;
        uint16_t len;
        uint8_t  data[UDX_MSS];


        ~Segment() = default;
        Segment(const Segment&) = delete;
        Segment(const Segment&&) = delete;
        Segment operator=(const Segment&) = delete;
        Segment operator=(const Segment&&) = delete;


        Segment() {
            ::memset(this, 0, sizeof(Segment));
        }


        Segment(uint8_t cmd, uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t acc, uint8_t frg, const uint8_t* data, size_t datalen)
            : cmd(cmd)
            , wnd(rcv_wnd)
            , sn(sn)
            , ts(ts)
            , fastack(0)
            , xmit(0)
            , resend_ts(0)
            , acc(acc)
            , frg(frg)
            , len(datalen) {

            if (cmd >= UDX_CMD_PSH && data && datalen > 0) {
                ::memcpy(this->data, data, datalen);
                return;
            }
            ::memset(this->data, 0, UDX_MSS);
        }


        __inline__ int size() {
            switch (cmd) {
            case UDX_CMD_PSH:
            case UDX_CMD_CON: return UDX_PSH_MIN + len;
            case UDX_CMD_ACK: return UDX_ACK_LEN;
            case UDX_CMD_PIN:
            case UDX_CMD_PON: return UDX_HDR_LEN;
            default: return -1;
            }
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


        static __inline__ int decode(Segment** seg, const uint8_t* buf, int buflen) {
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
            if (wnd > UDX_RWND_MAX) {
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


        __inline__ int encode(uint8_t* buf, int buflen) {
            ASSERT(buf);

            if (buflen < UDX_HDR_LEN) {
                return -1;
            }

            if (wnd > UDX_RWND_MAX) {
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


    static Ptr create(uint32_t uid) {
        return Ptr(new Udx(uid));
    }


    ~Udx() {
        while (rcv_buf_.size() > 0) {
            auto itr = rcv_buf_.begin();
            delete itr->second;
            rcv_buf_.erase(itr);
        }

        while (snd_que_.size() > 0) {
            auto itr = snd_que_.begin();
            delete* itr;
            snd_que_.erase(itr);
        }

        while (snd_buf_.size() > 0) {
            auto itr = snd_buf_.begin();
            delete itr->second;
            snd_buf_.erase(itr);
        }

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

        int64_t now_ts = now_ms - start_ms_;
        int res = 0;
        Segment* seg;

        while (buflen > 0) {
            n = Segment::decode(&seg, p, buflen);
            if (n < 0) {
                return -3;
            }

            ASSERT(seg);
            if (seg->sn > last_sn_ || last_sn_ == 0) {
                rwnd_ = seg->wnd;
                last_sn_ = seg->sn;

                // 拥塞控制
                if (cwnd_ < ssthresh_) {
                    cwnd_ <<= 1;
                }
                else {
                    cwnd_++;
                }

                if (cwnd_ > UDX_SWND_MAX) {
                    cwnd_ = UDX_SWND_MAX;
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

            case UDX_CMD_PIN:
                res = _update_pin();
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
        Datagram* dg = Datagram::get(&addr_, addrlen_);

        uint64_t now_ts = now_ms - start_ms_;
        uint8_t* p = dg->data + UDX_UID_LEN;
        int nbuf = 0, n, idx = 0;
        Segment seg, *psh;

        Datagram* dgs[IO_SMSG_SIZE];


        if (!ack_que_.empty()) {
            uint8_t acc_flag = 1;
            for (auto& ack : ack_que_) {
                if (nbuf > UDX_MTU - UDX_ACK_LEN - UDX_UID_LEN) {
                    nbuf += _u24_encode(uid_, dg->data);
                    dg->datalen = nbuf;
                    dgs[idx++] = dg;
                    dg = Datagram::get(&addr_, addrlen_);
                    p = dg->data + UDX_UID_LEN;
                    nbuf = 0;
                }

                seg.cmd = UDX_CMD_ACK;
                seg.wnd = rcv_buf_.size() < UDX_RWND_MAX ? UDX_RWND_MAX - rcv_buf_.size() : 0;
                seg.sn = ack.first;
                seg.ts = ack.second;
                seg.acc = acc_flag;
                n = seg.encode(p, UDX_MTU - UDX_UID_LEN - nbuf);
                p += n;
                nbuf += n;
                if (acc_flag) {
                    acc_flag = 0;
                }

                if (pon_flag_) {
                    pon_flag_ = false;
                }
            }

            ack_que_.clear();
        }

        while (!snd_que_.empty()) {
            Segment* seg = snd_que_.front();
            seg->resend_ts = now_ts + rto_;
            seg->wnd = UDX_SWND_MAX - rcv_buf_.size();
            snd_buf_.insert(std::make_pair(seg->sn, seg));
            snd_que_.pop_front();
        }

        int needsend = 0, i = 0, to_resend = 0, fa_resend = 0;
        uint8_t snd_wnd = xq::tools::MIN(cwnd_, rwnd_);

        if (snd_wnd > 0) {
            for (auto& itr : snd_buf_) {
                psh = itr.second;

                if (i > snd_wnd) {
                    break;
                }

                if (nbuf > UDX_MTU - UDX_PSH_MIN - UDX_UID_LEN - psh->len) {
                    nbuf += _u24_encode(uid_, dg->data);
                    dg->datalen = nbuf;
                    dgs[idx++] = dg;
                    dg = Datagram::get(&addr_, addrlen_);
                    p = dg->data + UDX_UID_LEN;
                    nbuf = 0;
                }

                if (psh->xmit == 0) {
                    // 第一次发送
                    needsend = 1;
                }
                else if (psh->xmit >= UDX_RXMIT_MAX) {
                    // 超过最大重传次数
                    return -1;
                }
                else if (psh->resend_ts <= now_ts) {
                    // 超时重传
                    needsend = 1;
                    psh->resend_ts += xq::tools::MID(UDX_RTO_MIN, (int)(rto_ * 1.3), UDX_RTO_MAX);

                    // TODO: 超时重传, 这里需要重新计算cwnd.
                    snd_wnd = xq::tools::MIN(cwnd_, rwnd_);
                    to_resend++;
                }
                else if (psh->fastack >= UDX_FAS_MAX) {
                    needsend = 1;
                    // TODO: 快重传, 这里需要进入快恢复
                    psh->fastack = 0;
                    fa_resend++;
                }

                if (needsend) {
                    // 所有需要重新发送的包 都需要打上当前时间戳.
                    psh->ts = now_ts;
                    psh->wnd = rcv_buf_.size() < UDX_RWND_MAX ? UDX_RWND_MAX - rcv_buf_.size() : 0;
                    n = psh->encode(p, UDX_MTU - UDX_UID_LEN - nbuf);
                    p += n;
                    nbuf += n;
                    psh->xmit++;
                    needsend = 0;
                    i++;
                    if (pon_flag_) {
                        pon_flag_ = false;
                    }
                }
            } // for
        }

        if (rwnd_ == 0) {
            seg.cmd = UDX_CMD_PIN;
            seg.ts = now_ts;
            seg.sn = snd_nxt_++;
            seg.wnd = rcv_buf_.size() < UDX_RWND_MAX ? UDX_RWND_MAX - rcv_buf_.size() : 0;
            n = seg.encode(p, UDX_MTU - UDX_UID_LEN - nbuf);
            p += n;
            nbuf += n;
            if (pon_flag_) {
                pon_flag_ = false;
            }
        }

        if (pon_flag_) {
            seg.cmd = UDX_CMD_PON;
            seg.ts = now_ts;
            seg.sn = snd_nxt_++;
            seg.wnd = rcv_buf_.size() < UDX_RWND_MAX ? UDX_RWND_MAX - rcv_buf_.size() : 0;
            n = seg.encode(p, UDX_MTU - UDX_UID_LEN - nbuf);
            p += n;
            nbuf += n;
        }

        ASSERT(nbuf == 0 || nbuf >= UDX_HDR_LEN);
        if (nbuf == 0) {
            Datagram::put(dg);
            return 0;
        }

        nbuf += _u24_encode(uid_, dg->data);
        dg->datalen = nbuf;
        dgs[idx++] = dg;
        
        // TODO: flush

        return 0;
    }


private:
    Udx(uint32_t uid)
        : addr_({0,{0}})
        , addrlen_(sizeof(addr_))
        , pon_flag_(false)
        , cwnd_(UDX_RWND_MIN)
        , rwnd_(UDX_RWND_MIN)
        , ssthresh_(UDX_SSTHRESH_INIT)
        , rto_(UDX_RTO_MIN)
        , srtt_(0)
        , rttvar_(0)
        , uid_(uid)
        , start_ms_(xq::tools::now_ms())
        , last_sn_(0)
        , rcv_nxt_(0)
        , snd_nxt_(0)
    {}


    void _reset(int64_t now_ms) {
        cwnd_ = UDX_RWND_MIN;
        start_ms_ = now_ms;
        rto_ = UDX_RTO_MIN;
        snd_nxt_ = rcv_nxt_ = last_sn_ = srtt_ = 0;

        while (rcv_buf_.size() > 0) {
            auto itr = rcv_buf_.begin();
            delete itr->second;
            rcv_buf_.erase(itr);
        }

        while (snd_que_.size() > 0) {
            auto itr = snd_que_.begin();
            delete *itr;
            snd_que_.erase(itr);
        }
        
        while (snd_buf_.size() > 0) {
            auto itr = snd_buf_.begin();
            delete itr->second;
            snd_buf_.erase(itr);
        }
        
        ack_que_.clear();
    }


    __inline__ int _update_con(int64_t now_ms, Segment* new_seg, const sockaddr *addr, socklen_t addrlen) {
        if (rcv_nxt_ != new_seg->sn && 0 && addrlen != addrlen_ && ::memcmp(addr, &addr_, addrlen)) {
            return -1;
        }

        this->_reset(now_ms);
        return this->_update_psh(new_seg);
    }


    __inline__ int _update_pin() {
        pon_flag_ = true;
        return 0;
    }

    __inline__ int _update_psh(Segment* new_seg) {
        if (new_seg->sn >= rcv_nxt_ + UDX_RWND_MAX) {
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


    __inline__ int _update_ack(Segment* new_seg, uint64_t now_ts) {
        auto end = snd_buf_.find(new_seg->sn);

        if (end != snd_buf_.end()) {
            if (new_seg->acc) {
                for (auto itr = snd_buf_.begin(); itr != end;) {
                    delete itr->second;
                    snd_buf_.erase(itr++);
                }
            }
            else {
                for (auto itr = snd_buf_.begin(); itr != end; ++itr) {
                    itr->second->fastack++;
                }
            }

            delete end->second;
            snd_buf_.erase(end);
        }

        /* ---------------------
         * 这里可能会出现负值.
         * 例如: 
         *     1, 发送端发 psh[sn:1, ts:1] 的包.
         *     2, 对端收到后响应 ack[sn:1, ts:2], 但是该响但在网络中滞留.
         *     3, 发送端触发重传(可能是超时重传, 也可能是快速重传) re psh[sn:1, ts:3].
         *     4, 发送端此时收到滞留的 ack[sn:1, ts: 2], 这时就会出现RTT时间负数的情况.
         */
        int64_t rtt = now_ts - new_seg->ts;
        if (rtt < 0) {
            delete new_seg;
            return 0;
        }

        if (srtt_ == 0) {
            srtt_ = rtt;
            rttvar_ = rtt / 2;
        }
        else {
            int64_t delta = rtt - srtt_;
            if (delta < 0) {
                delta = -delta;
            }

            rttvar_ = (3 * rttvar_ + delta) / 4;
            srtt_ = (7 * srtt_ + rtt) / 8;
        }

        if (srtt_ < 1) {
            srtt_ = 1;
        }

        rto_ = xq::tools::MID(UDX_RTO_MIN, srtt_ + xq::tools::MAX(UDX_UPD_INT, 4 * rttvar_), UDX_RTO_MAX);
        delete new_seg;
        return 0;
    }


    sockaddr addr_;
    socklen_t addrlen_;

    bool pon_flag_;
    int cwnd_; // 本端拥塞窗口
    int rwnd_; // 对端接收窗口
    int ssthresh_;
    int rto_;
    int srtt_;
    int rttvar_;
    
    uint32_t uid_;
    uint64_t start_ms_;
    uint64_t last_sn_;
    uint64_t rcv_nxt_;
    uint64_t snd_nxt_;

    std::map<uint64_t, uint64_t> ack_que_;
    std::map<uint64_t, Segment*> rcv_buf_;
    std::deque<Segment*> snd_que_;
    std::map<uint64_t, Segment*> snd_buf_;
}; // class Xdg;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_XDG__
