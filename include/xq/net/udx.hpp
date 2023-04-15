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
constexpr uint8_t  UDX_WND_MIN = 1;
constexpr uint8_t  UDX_WND_MAX = 128;
constexpr uint32_t UDX_UID_MAX = 100000;
constexpr uint64_t UDX_SN_MAX  = 0x0000FFFFFFFFFFFF;
constexpr uint64_t UDX_TS_MAX  = 0x0000FFFFFFFFFFFF;
constexpr uint32_t UDX_RTO_MIN = 100;
constexpr uint32_t UDX_RTO_MAX = 45000;
constexpr uint64_t UDX_UPD_INT = 0;


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
    // ------------------------------------------------------------------ BEG Segment  ------------------------------------------------------------------
    struct Segment {
        uint8_t  cmd;
        uint8_t  wnd;
        uint64_t sn;
        uint64_t ts;

        union {
            uint8_t acc;
            struct {
                uint8_t  frg;
                uint16_t len;
                uint8_t  data[0];
            };
        };


        Segment() {
            ::memset(this, 0, sizeof(Segment));
        }


        static __inline__ Segment* get_pin(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            Segment* seg = (Segment*)::malloc(sizeof(Segment));
            seg->set_pin(rcv_wnd, sn, ts);
            return seg;
        }


        static __inline__ Segment* get_pon(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            Segment* seg = (Segment*)::malloc(sizeof(Segment));
            seg->set_pon(rcv_wnd, sn, ts);
            return seg;
        }


        static __inline__ Segment* get_ack(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t acc) {
            Segment* seg = (Segment*)::malloc(sizeof(Segment));
            seg->set_ack(rcv_wnd, sn, ts, acc);
            return seg;
        }


        static __inline__ Segment* get_psh(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t frg, const uint8_t* data, size_t datalen) {
            if (datalen > 0 && !data) {
                return nullptr;
            }

            Segment* seg = (Segment*)::malloc(sizeof(Segment) + datalen);
            seg->set_psh(rcv_wnd, sn, ts, frg, data, datalen);
            return seg;
        }


        static __inline__ void put(const Segment* seg) {
            if (seg) {
                ::free(*(Segment**)&seg);
            }
        }


        static __inline__ int decode(Segment** seg, const uint8_t* buf, size_t buflen) {
            if (!buf) {
                return -1;
            }

            if (buflen < UDX_HDR_LEN) {
                return -2;
            }

            const uint8_t* p = buf;
            uint8_t cmd, wnd, acc, frg;
            uint16_t len = 0;
            uint64_t sn = 0, ts = 0;

            p += Udx::_u8_decode(p, &cmd);
            if (cmd < UDX_CMD_PIN || cmd > UDX_CMD_PSH) {
                return -3;
            }
        
            p += Udx::_u8_decode(p, &wnd);
            if (wnd > UDX_WND_MAX) {
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
            else if (cmd == UDX_CMD_PSH) {
                if (buflen < UDX_PSH_MIN) {
                    return -9;
                }

                p += Udx::_u8_decode(p, &frg);
                if (frg > UDX_FRG_MAX) {
                    return -10;
                }

                p += Udx::_u16_decode(p, &len);
                if (len > UDX_MSS) {
                    return -11;
                }

                if (buflen < len + UDX_PSH_MIN) {
                    return -12;
                }

                if (len > 0) {
                    p += len;
                }
            }

            switch (cmd) {
            case UDX_CMD_PIN: *seg = Segment::get_pin(wnd, sn, ts); break;
            case UDX_CMD_PON: *seg = Segment::get_pon(wnd, sn, ts); break;
            case UDX_CMD_ACK: *seg = Segment::get_ack(wnd, sn, ts, acc); break;
            case UDX_CMD_PSH: *seg = Segment::get_psh(wnd, sn, ts, frg, p - len, len); break;
            }
            return p - buf;
        }


        __inline__ int encode(uint8_t *buf, size_t buflen) {
            ASSERT(buf);

            if (buflen < UDX_HDR_LEN) {
                return -1;
            }

            if (wnd > UDX_WND_MAX) {
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


        __inline__ void set_pin(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            this->cmd = UDX_CMD_PIN;
            this->wnd = rcv_wnd;
            this->sn = sn;
            this->ts = ts;
        }


        __inline__ void set_pon(uint8_t rcv_wnd, uint64_t sn, uint64_t ts) {
            this->cmd = UDX_CMD_PON;
            this->wnd = rcv_wnd;
            this->sn = sn;
            this->ts = ts;
        }


        __inline__ void set_ack(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t acc) {
            ASSERT(sn < UDX_SN_MAX && ts < UDX_TS_MAX);
            this->cmd = UDX_CMD_ACK;
            this->wnd = rcv_wnd;
            this->sn = sn;
            this->ts = ts;
            this->acc = acc;
        }


        __inline__ void set_psh(uint8_t rcv_wnd, uint64_t sn, uint64_t ts, uint8_t frg, const uint8_t* data, size_t datalen) {
            this->cmd = UDX_CMD_PSH;
            this->wnd = rcv_wnd;
            this->sn = sn;
            this->ts = ts;
            this->frg = frg;
            this->len = datalen;

            if (datalen > 0) {
                ASSERT(data && datalen <= UDX_MSS);
                ::memcpy(this->data, data, datalen);
            }
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
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    void set_addr(const std::string& remote) {
        ASSERT(xq::net::str2addr(remote, &addr_, &addrlen_));
    }


    int input(const uint8_t* buf, size_t buflen, int64_t now_ms) {
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
        Segment* seg = nullptr;

        while (buflen > 0) {
            n = Segment::decode(&seg, p, buflen);
            if (n < 0) {
                return -3;
            }

            ASSERT(seg);
            if (seg->sn > last_sn_) {
                rmt_wnd_ = seg->wnd;
                last_sn_ = seg->sn;
            }

            if (seg->cmd == UDX_CMD_ACK) {
                res = _update_ack(seg, now_ts);
            }
            else if (seg->cmd == UDX_CMD_PSH) {
                res = _update_push(seg);
            }

            p += n;
            buflen -= n;
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

        for (int i = 1; i <= n; i++) {
            uint16_t len = i == n ? datalen : UDX_MSS;
            Segment* seg = Segment::get_psh(0, snd_nxt_++, 0, n - i, data, len);
            snd_que_.emplace_back(seg);
            data += len;
            datalen -= len;
        }

        return 0;
    }


    void flush_ack(int64_t now_ms) {
        if (ack_que_.empty()) {
            return;
        }

        UdpSession::Datagram* dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);

        uint64_t cur_ts = now_ms - start_ms_;

        uint8_t* p = dg->data;
        int buflen = UDX_MTU;
        int n = Udx::_u24_encode(uid_, p);
        p += n;
        buflen -= n;
        
        uint8_t acc_flag = 1;
        Segment seg;

        for (auto& ack : ack_que_) {
            if (buflen < UDX_ACK_LEN) {
                dg->datalen = UDX_MTU - buflen;
                ASSERT(!sess_->send(dg));

                dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                p = dg->data;
                buflen = UDX_MTU;
                n = Udx::_u24_encode(uid_, p);
                p += n;
                buflen += n;
            }

            seg.set_ack(rcv_buf_.size() < UDX_WND_MAX ? UDX_WND_MAX - rcv_buf_.size() : 0, ack.first, ack.second, acc_flag);
            n = seg.encode(p, buflen);
            p += n;
            buflen -= n;
            if (acc_flag) {
                acc_flag = 0;
            }
        }

        dg->datalen = UDX_MTU - buflen;
        if (dg->datalen == 0) {
            delete dg;
            return;
        }
        
        ASSERT(!sess_->send(dg));
        ASSERT(!sess_->flush());
    }


    void flush_psh(int64_t now_ms) {
        static constexpr uint8_t SND_WND_MAX = UDX_WND_MAX / 4;

        if (snd_que_.empty() || snd_buf_.size() >= SND_WND_MAX) {
            return;
        }

        UdpSession::Datagram* dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);

        uint64_t now_ts = now_ms - start_ms_;

        uint8_t* p = dg->data;
        int buflen = UDX_MTU;
        int n = Udx::_u24_encode(uid_, p);
        p += n;
        buflen -= n;

        while (!snd_que_.empty() && snd_buf_.size() < SND_WND_MAX) {
            Segment* seg = snd_que_.front();

            if (buflen < seg->len + UDX_PSH_MIN) {
                dg->datalen = UDX_MTU - buflen;
                ASSERT(!sess_->send(dg));
                dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                p = dg->data;
                buflen = UDX_MTU;
                n = Udx::_u24_encode(uid_, p);
                p += n;
                buflen -= n;
            }

            seg->ts = now_ts;
            seg->wnd = rcv_buf_.size() < UDX_WND_MAX ? UDX_WND_MAX - rcv_buf_.size() : 0;
            n = seg->encode(p, buflen);
            p += n;
            buflen -= n;

            snd_que_.pop_front();
        }

        dg->datalen = UDX_MTU - buflen;
        if (dg->datalen == 0) {
            delete dg;
            return;
        }

        ASSERT(!sess_->send(dg));
        ASSERT(!sess_->flush());
    }


    void flush(int64_t now_ms) {
        flush_ack(now_ms);
        flush_psh(now_ms);
    }


private:
    Udx(uint32_t uid, UdpSession::Ptr &sess)
        : sess_(sess)
        , addr_({0,{0}})
        , addrlen_(sizeof(addr_))
        , snd_wnd_(UDX_WND_MIN)
        , rmt_wnd_(0)
        , uid_(uid)
        , srtt_(0)
        , rttvar_(0)
        , rto_(0)
        , start_ms_(xq::tools::now_ms())
        , last_sn_(0)
        , rcv_nxt_(0)
        , snd_nxt_(0)
    {
        std::printf("start_time: %lld\n", start_ms_);
    }


    int _update_push(Segment* new_seg) {
        if (new_seg->sn > rcv_nxt_ + UDX_WND_MAX) {
            std::printf("seg->sn[%llu] > rcv_nxt[%llu] + %d\n", new_seg->sn, rcv_nxt_, UDX_WND_MAX);
            Segment::put(new_seg);
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
            Segment::put(new_seg);
            return 0;
        }

        auto seg_itr = rcv_buf_.find(new_seg->sn);
        if (seg_itr != rcv_buf_.end()) {
            Segment::put(new_seg);
            return 0;
        }

        rcv_buf_.insert(std::make_pair(new_seg->sn, new_seg));
        return 0;
    }


    int _update_ack(const Segment* seg, uint64_t now_ts) {
        std::printf("recv ack: %d\n", seg->sn);

        auto itr = snd_buf_.find(seg->sn);
        if (itr != snd_buf_.end()) {
            if (seg->acc) {
                snd_buf_.erase(snd_buf_.begin(), itr);
            }
            snd_buf_.erase(itr);
        }

        uint64_t rtt = now_ts - seg->ts;
        if (rtt < 0) {
            Segment::put(seg);
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
        //std::printf("> SN: %llu, RTT: %d, SRTT: %d, RTO: %u --------------- inf_buf: %llu\n", seg->sn, rtt, srtt_, rto_, snd_buf_.size());

        Segment::put(seg);
        return 0;
    }


    UdpSession::Ptr sess_;
    sockaddr addr_;
    socklen_t addrlen_;

    uint8_t  snd_wnd_, rmt_wnd_;
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
