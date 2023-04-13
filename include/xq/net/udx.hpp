#ifndef __XQ_NET_XDG__
#define __XQ_NET_XDG__


#include <deque>
#include <unordered_set>
#include "xq/net/udp_session.hpp"


namespace xq {
namespace net {


constexpr size_t   UDX_FHD_LEN = 16;
constexpr size_t   UDX_MTU     = UDP_DGX_SIZE;
constexpr size_t   UDX_MSS     = (UDX_MTU - UDX_FHD_LEN - sizeof(uint8_t) - sizeof(uint8_t) - sizeof(uint16_t)) / 16 * 16;
constexpr size_t   UDX_ACK_LEN = 14;
constexpr size_t   UDX_PSH_MIN = 4;
constexpr size_t   UDX_PSH_MAX = UDX_MSS + UDX_PSH_MIN;
constexpr uint8_t  UDX_FRG_MAX = 256 * 1024 / UDX_MSS;
constexpr size_t   UDX_MSG_MAX = UDX_FRG_MAX * UDX_MSS;
constexpr size_t   UDX_FSG_MAX = UDX_MSS / UDX_ACK_LEN;
constexpr uint8_t  UDX_CMD_PSH = 0x11;
constexpr uint8_t  UDX_CMD_ACK = 0x12;
constexpr uint16_t UDX_WND_MIN = 2;
constexpr uint16_t UDX_WND_MAX = 128;
constexpr uint32_t UDX_UID_MAX = 100000;
constexpr uint64_t UDX_SN_MAX  = 281474976710656;
constexpr uint64_t UDX_TS_MAX  = 281474976710656;
constexpr uint32_t UDX_RTO_MIN = 100;
constexpr uint32_t UDX_RTO_MAX = 45000;
constexpr uint32_t UDX_INTERVAL = 0;


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
        uint8_t cmd; // 指令
        union {
            struct {
                uint8_t  acc; // 是否为累积确认
                uint64_t sn;  // ACK Frame Number
                uint64_t ts;  // ACK TimeStamp(毫秒)
            } ack;

            struct {
                uint8_t  frg; // 分片
                uint16_t len; // 长度
                uint8_t  data[UDX_MSS]; // 数据
            } psh;
        } payload;

        uint64_t sn;
        uint64_t rcv_ts;


        int __inline__ decode(const uint8_t* buf, size_t buflen) {
            ASSERT(buf && buflen >= UDX_PSH_MIN);

            const uint8_t* p = buf;

            // 解析cmd & flg
            p += Udx::_u8_decode(p, &cmd);
            
            if (cmd == UDX_CMD_PSH) {
                // 解码 PUSH
                if (buflen < UDX_PSH_MIN) {
                    return -1;
                }

                p += Udx::_u8_decode(p, &payload.psh.frg);
                if (payload.psh.frg > UDX_FRG_MAX) {
                    return -1;
                }

                p += Udx::_u16_decode(p, &payload.psh.len);
                if (payload.psh.len > UDX_MSS) {
                    return -1;
                }

                ::memcpy(payload.psh.data, p, payload.psh.len);
                p += payload.psh.len;
            }
            else if (cmd == UDX_CMD_ACK) {
                // 解码 ACK
                if (buflen < UDX_ACK_LEN) {
                    return -1;
                }

                p += Udx::_u8_decode(p, &payload.ack.acc);
                if (payload.ack.acc != 0 && payload.ack.acc != 1) {
                    return -1;
                }

                p += Udx::_u48_decode(p, &payload.ack.sn);
                p += Udx::_u48_decode(p, &payload.ack.ts);
            }
            else {
                return -1;
            }

            return p - buf;
        }


        int __inline__ encode(uint8_t *buf, size_t buflen) {
            ASSERT(buf);

            uint8_t* p = buf;

            if (cmd == UDX_CMD_PSH) {
                if (buflen < UDX_PSH_MIN) {
                    return -1;
                }

                p += Udx::_u8_encode(cmd, p);
                p += Udx::_u8_encode(payload.psh.frg, p);
                p += Udx::_u16_encode(payload.psh.len, p);

                if (payload.psh.frg > 0 && payload.psh.len != UDX_MSS) {
                    return -1;
                }

                ::memcpy(p, payload.psh.data, payload.psh.len);
                p += payload.psh.len;
            }
            else if (cmd == UDX_CMD_ACK) {
                if (buflen < UDX_ACK_LEN) {
                    return -1;
                }

                p += Udx::_u8_encode(cmd, p);
                p += Udx::_u8_encode(payload.ack.acc, p);
                p += Udx::_u48_encode(payload.ack.sn, p);
                p += Udx::_u48_encode(payload.ack.ts, p);
            }
            else {
                return -1;
            }

            return p - buf;
        }


        explicit Segment() {
            ::memset(this, 0, sizeof(*this));
        }


        void __inline__ set_ack(uint8_t acc, uint64_t sn, uint64_t ts) {
            ASSERT(sn <= UDX_SN_MAX && ts <= UDX_TS_MAX);
            cmd = UDX_CMD_ACK;
            payload.ack.acc = acc;
            payload.ack.sn = sn;
            payload.ack.ts = ts;
        }


        void __inline__ set_psh(uint8_t frg, const uint8_t* data, size_t datalen) {
            ASSERT(data && datalen <= UDX_MSS && frg < UDX_FRG_MAX);
            cmd = UDX_CMD_PSH;
            ::memcpy(payload.psh.data, data, datalen);
            payload.psh.len = datalen;
            payload.psh.frg = frg;
        }


        friend class Udx;
        Segment(const Segment&) = delete;
        Segment(const Segment&&) = delete;
        Segment& operator=(const Segment&) = delete;
        Segment& operator=(const Segment&&) = delete;
    };
    // ------------------------------------------------------------------ END Segment ------------------------------------------------------------------


    // ------------------------------------------------------------------ BEG Frame ------------------------------------------------------------------
    // | UID: 24 | WND: 8 | TS: 48 |
    struct Frame {
        uint32_t uid;
        uint8_t  wnd;
        uint64_t sn;
        uint64_t ts;

        Segment* segs[UDX_FSG_MAX];
        size_t   nsegs;

        uint32_t fastack;
        uint32_t xmit;
        uint64_t resend_ts;


        static Frame* decode(const uint8_t* buf, size_t buflen) {
            ASSERT(buf && buflen >= UDX_FHD_LEN && buflen <= UDX_MTU);

            const uint8_t* p = buf;
            uint8_t wnd;
            int n;
            uint32_t uid = 0;

            p += Udx::_u24_decode(p, &uid);
            if (uid > UDX_UID_MAX) {
                return nullptr;
            }

            p += Udx::_u8_decode(p, &wnd);
            if (wnd > UDX_WND_MAX) {
                return nullptr;
            }

            Frame* frm = new Frame(uid, wnd);
            p += Udx::_u48_decode(p, &frm->sn);
            p += Udx::_u48_decode(p, &frm->ts);

            buflen -= UDX_FHD_LEN;

            while (buflen > 0) {
                Segment* seg = new Segment;
                n = seg->decode(p, buflen);
                if (n < 0) {
                    delete seg;
                    break;
                }

                if (seg->cmd == UDX_CMD_PSH) {
                    seg->sn = frm->sn;
                }
                frm->segs[frm->nsegs++] = seg;
                p += n;
                buflen -= n;
            }

            if (buflen) {
                delete frm;
                return nullptr;
            }

            return frm;
        }


        static int encode(const Frame* frm, uint8_t* buf, size_t buflen) {
            ASSERT(frm && buf && buflen <= UDX_MTU);

            int ret;
            Segment* seg;
            uint8_t* p = buf;
            p += Udx::_u24_encode(frm->uid, p);
            p += Udx::_u8_encode(frm->wnd, p);
            p += Udx::_u48_encode(frm->sn, p);
            p += Udx::_u48_encode(frm->ts, p);

            buflen -= UDX_FHD_LEN;

            for (size_t i = 0, n = frm->nsegs; i < n; i++) {
                seg = frm->segs[i];
                ret = seg->encode(p, buflen);
                p += ret;
                buflen -= ret;
            }

            return p - buf;
        }


        int encode(uint8_t* buf, size_t buflen) {
            return encode(this, buf, buflen);
        }


        explicit Frame(uint32_t uid = 0, uint8_t wnd = 0, uint64_t sn = 0, uint64_t ts = 0)
            : uid(uid)
            , wnd(wnd)
            , sn(sn)
            , ts(ts)
            , nsegs(0)
            , fastack(0)
            , xmit(0)
            , resend_ts(0) {
            ::memset(segs, 0, sizeof(segs));
        }


        void __inline__ add_segment(const Segment* seg) {
            this->segs[this->nsegs++] = const_cast<Segment*>(seg);
        }


        friend class Udx;
        Frame(const Frame&) = delete;
        Frame(const Frame&&) = delete;
        Frame& operator=(const Frame&) = delete;
        Frame& operator=(const Frame&&) = delete;
    };
    // ------------------------------------------------------------------ END Frame ------------------------------------------------------------------


    static Ptr create(uint32_t uid, UdpSession::Ptr &sess) {
        return Ptr(new Udx(uid, sess));
    }


    ~Udx() {
        snd_que_.clear();
        snd_buf_.clear();
        rcv_buf_.clear();
        ack_que_.clear();
    }


    void set_addr(sockaddr *addr, socklen_t addrlen) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    void set_addr(const std::string& remote) {
        ASSERT(xq::net::str2addr(remote, &addr_, &addrlen_));
    }


    int input(const uint8_t* buf, size_t buflen, int64_t now_ms) {
        ASSERT(buf && buflen <= UDX_MTU);

        if (buflen < UDX_FHD_LEN) {
            return -1;
        }

        const uint8_t* p = buf;
        int cur_ts = now_ms - start_ms_, res = 0;
        Segment* seg;

        Frame* frm = Frame::decode(buf, buflen);
        if (!frm) {
            return -1;
        }

        int n = _update_frame(frm);
        if (n < 0) {
            return -1;
        }

        for (int i = 0; i < n; i++) {
            seg = frm->segs[i];
            if (res) {
                delete seg;
                continue;
            }

            seg->rcv_ts = cur_ts;
            if (seg->cmd == UDX_CMD_PSH) {
                res = _update_push(seg);
            }
            else {
                res = _update_ack(seg);
            }
        }

        delete frm;
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
            if (seg->payload.psh.frg == 0) {
                ok = true;
                break;
            }
        }

        if (ok) {
            rcv_nxt_ = nxt;
            rcv_buf_.erase(rcv_buf_.begin(), itr);

            uint8_t *p = buf;
            for (size_t i = 0; i < nsegs; i++) {
                Segment* seg = segs[i];
                ::memcpy(p, seg->payload.psh.data, seg->payload.psh.len);
                p += seg->payload.psh.len;
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
            Segment* seg = new Segment;
            seg->set_psh(n - i, data, len);
            snd_que_.emplace_back(seg);
            data += len;
            datalen -= len;
        }

        return 0;
    }


    void flush(int64_t now_ms) {
        UdpSession::Datagram *dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);

        uint64_t cur_ts = now_ms - start_ms_;
        Segment seg;
        Frame frm(uid_, cwnd_, snd_nxt_++, cur_ts);

        uint8_t* p = dg->data;
        int buflen = UDX_MTU;

        int n = frm.encode(p, buflen);
        ASSERT(n > 0);
        p += n;
        buflen -= n;

        // ACK 响应
        if (!ack_que_.empty()) {
            uint8_t acc_flag = 1;

            for (auto &ack: ack_que_) {
                if (buflen < UDX_ACK_LEN) {
                    dg->datalen = UDX_MTU - buflen;
                    sess_->send(dg);
                    dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                    p = dg->data;
                    buflen = UDX_MTU;

                    frm.wnd = cwnd_;
                    frm.sn = snd_nxt_++;

                    n = frm.encode(p, buflen);
                    p += n;
                    buflen -= n;
                }

                seg.set_ack(acc_flag, ack.first, ack.second);
                n = seg.encode(p, buflen);
                p += n;
                buflen -= n;
                if (acc_flag) {
                    acc_flag = 0;
                }
            }

            ack_que_.clear();
        }

        // Udx数据发送
        while (!snd_que_.empty()) {
            Segment* pseg = snd_que_.front();

            if (buflen < pseg->payload.psh.len + UDX_PSH_MIN) {
                dg->datalen = UDX_MTU - buflen;
                ASSERT(sess_->send(dg) >= 0);
                dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                p = dg->data;
                buflen = UDX_MTU;

                frm.wnd = cwnd_;
                frm.sn = snd_nxt_++;

                n = frm.encode(p, buflen);
                p += n;
                buflen -= n;
            }

            n = pseg->encode(p, buflen);
            p += n;
            buflen -= n;

            snd_que_.pop_front();
        }

        dg->datalen = UDX_MTU - buflen;
        if (dg->datalen == 0) {
            delete dg;
        }
        else {
            ASSERT(sess_->send(dg) >= 0);
            sess_->flush();
        }
    }


private:
    Udx(uint32_t uid, UdpSession::Ptr &sess)
        : sess_(sess)
        , addr_({0,{0}})
        , addrlen_(sizeof(addr_))
        , cwnd_(UDX_WND_MIN)
        , rmt_wnd_(0)
        , uid_(uid)
        , srtt_(0)
        , rttvar_(0)
        , rto_(0)
        , start_ms_(xq::tools::now_ms())
        , last_sn_(0)
        , rcv_nxt_(0)
        , snd_nxt_(0)
    {}


    int _update_frame(Frame* frm) {
        if (frm->sn > rcv_nxt_ + cwnd_) {
            return -1;
        }

        if (frm->sn > last_sn_) {
            rmt_wnd_ = frm->wnd;
        }

        // 删除前一个连序的ACK
        auto ack_itr = ack_que_.find(frm->sn - 1);
        if (ack_itr != ack_que_.end()) {
            ack_que_.erase(ack_itr);
        }

        // 如果后一个连序的ACK不存在, 则加入ACK队列
        ack_itr = ack_que_.find(frm->sn + 1);
        if (ack_itr == ack_que_.end()) {
            ack_que_[frm->sn] = frm->ts;
        }

        return frm->sn < rcv_nxt_ ? 0 : frm->nsegs;
    }


    int _update_push(Segment* new_seg) {
        uint64_t vsn = (new_seg->sn << 8) | (UDX_FRG_MAX - new_seg->payload.psh.frg);
        auto itr = rcv_buf_.find(vsn);
        if (itr != rcv_buf_.end()) {
            delete new_seg;
            return 0;
        }
        
        rcv_buf_.insert(std::make_pair(vsn, new_seg));
        return 0;
    }


    int _update_ack(const Segment* seg) {
        auto itr = snd_buf_.find(seg->payload.ack.sn);
        if (itr != snd_buf_.end()) {
            if (seg->payload.ack.acc) {
                snd_buf_.erase(snd_buf_.begin(), itr);
            }
            snd_buf_.erase(itr);
        }

        int32_t rtt = seg->rcv_ts - seg->payload.ack.ts;
        if (rtt < 0) {
            delete seg;
            return -1;
        }

        if (srtt_ == 0) {
            srtt_ = rtt;
            rttvar_ = rtt / 2;
        }
        else {
            int32_t delta = rtt - srtt_;
            if (delta < 0) {
                delta = -delta;
            }

            rttvar_ = (3 * rttvar_ + delta) / 4;
            srtt_ = (7 * srtt_ + rtt) / 8;

            if (srtt_ < 1) {
                srtt_ = 1;
            }
        }

        uint32_t tmp = srtt_ + xq::tools::MAX(UDX_INTERVAL, 4 * rttvar_);
        rto_ = xq::tools::MID(UDX_RTO_MIN, tmp, UDX_RTO_MAX);
        std::printf("> SN: %lu, RTT: %u, SRTT: %u, RTO: %u --------------- inf_buf: %lu\n", seg->payload.ack.sn, rtt, srtt_, rto_, snd_buf_.size());

        delete seg;
        return 0;
    }


    UdpSession::Ptr sess_;
    sockaddr addr_;
    socklen_t addrlen_;

    uint8_t  cwnd_, rmt_wnd_;
    uint32_t uid_, srtt_, rttvar_, rto_;
    uint64_t start_ms_, last_sn_;
    uint64_t rcv_nxt_, snd_nxt_;

    std::map<uint64_t, uint64_t> ack_que_;
    std::map<uint64_t, Segment*> rcv_buf_;
    std::deque<Segment*> snd_que_;
    std::map<uint64_t, Frame*> snd_buf_;
}; // class Xdg;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_XDG__
