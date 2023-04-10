#ifndef __XQ_NET_XDG__
#define __XQ_NET_XDG__


#include <unordered_set>
#include "xq/net/udp_session.hpp"


namespace xq {
namespace net {


constexpr size_t   UDX_HEAD_SIZE = 24;
constexpr size_t   UDX_MSS = 88 * 16;
constexpr size_t   UDX_MTU = UDX_MSS + UDX_HEAD_SIZE + 2;
constexpr size_t   UDX_SEG_SIZE = 93;
constexpr size_t   UDX_MAX_DATA_SIZE = UDX_SEG_SIZE * UDX_MSS;
constexpr uint8_t  UDX_CMD_PSH = 0x11;
constexpr uint8_t  UDX_CMD_ACK = 0x12;
constexpr uint8_t  UDX_CMD_PIN = 0x13;
constexpr uint8_t  UDX_CMD_PON = 0x14;
constexpr uint16_t UDX_MIN_WND = 32;
constexpr uint16_t UDX_MAX_WND = 512;
constexpr uint32_t UDX_MAX_RID = 100000;
constexpr uint64_t UDX_MAX_SN = 281474976710656;
constexpr uint64_t UDX_MAX_TS = 281474976710656;
constexpr uint16_t UDX_RCV_WND = 256;
constexpr uint32_t UDX_MIN_RTO = 100;
constexpr uint32_t UDX_MAX_RTO = 45000;
constexpr uint32_t UDX_INTERVAL = 0;
constexpr uint32_t UDX_CWND_INIT = 4;


class Udx {
public:
    typedef std::pair<uint64_t, uint64_t> Ack;
    typedef std::shared_ptr<Udx> Ptr;


    // ------------------------------------------------------------------ BEG Segment  ------------------------------------------------------------------
    // COM: 1 | CMD: 7 | RID: 24 | SN: 48 | TS: 48 | UNA: 48  | WND: 16 | @LEN: 16 | @PAYLOAD: n |
    struct Segment {
        uint8_t  com;
        uint8_t  cmd;
        uint32_t rid;
        uint64_t sn;
        uint64_t ts;
        uint64_t una;
        uint16_t wnd;
        uint16_t len;
        uint32_t fastack;
        uint32_t xmit;
        uint64_t resend_ts;

        uint8_t  payload[UDX_MSS];


        Segment() {
            ::memset(this, 0, sizeof(*this));
        }


        static int decode(Segment* seg, const uint8_t* data, size_t datalen) {
            if (datalen < UDX_HEAD_SIZE) {
                return -1;
            }

            const uint8_t* p = data;

            uint8_t com_cmd = 0, com = 0, cmd = 0;
            uint16_t wnd = 0, len = 0;
            uint32_t rid = 0;

            p += _u8_decode(p, &com_cmd);
            com = com_cmd >> 7;
            cmd = com_cmd & 0x7F;

            if (cmd > UDX_CMD_PON || cmd < UDX_CMD_PSH) {
                return -1;
            }

            p += _u24_decode(p, &rid);
            if (rid == 0 || rid > UDX_MAX_RID) {
                return -2;
            }

            p += _u48_decode(p, &seg->sn);
            p += _u48_decode(p, &seg->ts);
            p += _u48_decode(p, &seg->una);
            p += _u16_decode(p, &wnd);
            if (wnd > UDX_MAX_WND) {
                return -3;
            }

            seg->com = com;
            seg->cmd = cmd;
            seg->rid = rid;
            seg->wnd = wnd;

            if (cmd == UDX_CMD_PSH) {
                p += _u16_decode(p, &len);
                seg->len = len;
                ::memcpy(seg->payload, p, len);
                p += len;
            }

            return p - data;
        }


        static int encode(const Segment* seg, uint8_t* data, size_t datalen) {
            ASSERT(data && datalen > 0);

            int len = seg->len;

            if (len && seg->cmd != UDX_CMD_PSH && seg->cmd != UDX_CMD_ACK) {
                return -1;
            }

            uint8_t* p = data;

            p += _u8_encode((seg->com << 7) | seg->cmd, p);
            p += _u24_encode(seg->rid, p);
            p += _u48_encode(seg->sn, p);
            p += _u48_encode(seg->ts, p);
            p += _u48_encode(seg->una, p);
            p += _u16_encode(seg->wnd, p);

            if (len) {
                p += _u16_encode(seg->len, p);
                ::memcpy(p, seg->payload, len);
                p += len;
            }

            return p - data;
        }


    private:
        static int _u48_decode(const uint8_t* p, uint64_t* v) {
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


        static int _u48_encode(uint64_t v, uint8_t* p) {
            uint8_t* tmp = (uint8_t*)&v; // 00 00 01 02 03 04 05 06 =>
#if X_BIG_ENDIAN || X_MUST_ALIGN
            *p = *(tmp + 7);
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


        static int _u24_decode(const uint8_t* p, uint32_t* v) {
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


        static int _u24_encode(uint32_t v, uint8_t* p) {
            uint8_t* tmp = (uint8_t*)&v;
#if X_BIG_ENDIAN || X_MUST_ALIGN
            *p = *(tmp + 3);
            *(p + 1) = *(tmp + 2);
            *(p + 2) = *(tmp + 1);
#else
            ::memcpy(p, tmp, 3);
#endif
            return 3;
        }


        static int _u16_decode(const uint8_t* p, uint16_t* v) {
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
            *p = *(tmp + 1);
            *(p + 1) = *tmp;
#else
            ::memcpy(p, tmp, 2);
#endif
            return 2;
        }


        static int _u8_decode(const uint8_t* p, uint8_t* v) {
            *v = *p;
            return 1;
        }


        static int _u8_encode(uint8_t v, uint8_t* p) {
            *p = v;
            return 1;
        }
    };
    // ------------------------------------------------------------------ END Segment ------------------------------------------------------------------


    static Ptr create(uint32_t rid, UdpSession::Ptr &sess) {
        return Ptr(new Udx(rid, sess));
    }


    ~Udx() {
        snd_buf_.clear();
        inf_buf_.clear();
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


    int input(const uint8_t* data, size_t datalen, int64_t now_ms) {
        if (datalen < UDX_HEAD_SIZE) {
            return -100;
        }

        size_t nleft = datalen;
        const uint8_t* p = data;

        int cur_ts = now_ms - on_ms_;

        while (nleft > 0) {
            // 该 seg堆对象会在 CMD处理函数中被删除或移动.
            Segment* seg = new Segment;
            int n = Segment::decode(seg, p, nleft);
            if (n < 0) {
                delete seg;
                return n;
            }

            p += n;
            nleft -= n;

            _update_una(seg->una);

            switch (seg->cmd) {
                case UDX_CMD_PSH: n = _update_push(seg); break;
                case UDX_CMD_ACK:  n = _update_ack(seg, cur_ts); break;
                case UDX_CMD_PIN: n = _update_ping(seg); break;
                case UDX_CMD_PON: n = _update_pong(seg); break;
                default: n = -101; break;
            }

            if (n < 0) {
                return n;
            }
        }

        return 0;
    }


    int recv(uint8_t* buf, size_t buflen) {
        ASSERT(buf && buflen > 0);

        bool com = false;
        Segment* segs[UDX_SEG_SIZE];
        size_t nsegs = 0;

        uint64_t nxt = rcv_nxt_;
        auto itr = rcv_buf_.begin();

        for (; itr != rcv_buf_.end(); ++itr, ++nxt) {
            if (com) {
                break;
            }

            auto &seg = itr->second;
            if (seg->sn != nxt) {
                return 0;
            }

            segs[nsegs++] = seg;
            if (seg->com) {
                com = true;
            }
        }

        if (com) {
            rcv_nxt_ = nxt;
            rcv_buf_.erase(rcv_buf_.begin(), itr);

            uint8_t *p = buf;
            for (size_t i = 0; i < nsegs; i++) {
                Segment* seg = segs[i];
                ::memcpy(p, seg->payload, seg->len);
                p += seg->len;
                delete seg;
            }

            return p - buf;
        }

        return 0;
    }


    int send(const uint8_t* data, size_t datalen) {
        ASSERT(data && datalen > 0 && datalen <= UDX_MAX_DATA_SIZE);

        int n = datalen > UDX_MSS ? (datalen + UDX_MSS - 1) / UDX_MSS : 1;
        int nrcv = rcv_buf_.size();

        for (int i = 0; i < n; i++) {
            Segment* seg = new Segment;
            bool is_last = i + 1 == n;

            if (is_last) {
                seg->com = 1;
            }

            seg->cmd = UDX_CMD_PSH;
            seg->rid = rid_;
            seg->sn = snd_nxt_++;
            seg->una = rcv_nxt_;
            seg->wnd = nrcv < UDX_RCV_WND ? nrcv : 0;
            seg->len = is_last ? datalen : UDX_MSS;
            ::memcpy(seg->payload, data, UDX_MSS);

            snd_buf_.insert(std::make_pair(seg->sn, seg));

            data += UDX_MSS;
            datalen -= UDX_MSS;
        }

        return 0;
    }


    void flush(int64_t now_ms) {
        UdpSession::Datagram *dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);

        uint8_t* p = dg->data;
        size_t nbuf = UDX_MTU;
        uint64_t cur_ts = now_ms - on_ms_;

        // ACK 响应
        if (!ack_que_.empty()) {
            for (auto &ack: ack_que_) {
                dg->datalen = UDX_MTU - nbuf;
                if (nbuf < UDX_HEAD_SIZE) {
                    sess_->send(dg);
                    dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                    p = dg->data;
                    nbuf = UDX_MTU;
                }

                int nrcv = rcv_buf_.size();

                Segment seg;
                seg.com = 1;
                seg.cmd = UDX_CMD_ACK;
                seg.rid = rid_;
                seg.sn = ack.first;
                seg.ts = ack.second;
                seg.una = rcv_nxt_;
                int wnd = UDX_RCV_WND - nrcv;
                if (wnd < 0) {
                    wnd = 0;
                }
                seg.wnd = wnd;

                int n = Segment::encode(&seg, p, nbuf);
                p += n;
                nbuf -= n;
            }
            ack_que_.clear();
        }

        // Udx数据发送
        for (auto itr = snd_buf_.begin(); itr != snd_buf_.end();) {
            auto seg = itr->second;

            dg->datalen = UDX_MTU - nbuf;
            if (nbuf < UDX_HEAD_SIZE) {
                sess_->send(dg);
                dg = UdpSession::Datagram::get(sess_.get(), &addr_, addrlen_);
                p = dg->data;
                nbuf = UDX_MTU;
            }

            seg->ts = cur_ts;
            if (seg->resend_ts == 0) {
                seg->resend_ts = (rto_ == 0 ? UDX_MIN_RTO : rto_) + cur_ts;
                int n = Segment::encode(seg, dg->data, nbuf);
                p += n;
                nbuf -= n;
                snd_buf_.erase(itr++);
                inf_buf_.insert(std::make_pair(seg->sn, seg));
            }
        }

        dg->datalen = UDX_MTU - nbuf;
        if (dg->datalen > 0) {
            sess_->send(dg);
            sess_->flush();
        }
        else {
            delete dg;
        }
    }


private:
    Udx(uint32_t rid, UdpSession::Ptr &sess)
        : sess_(sess)
        , addr_({0,{0}})
        , addrlen_(sizeof(addr_))
        , cwnd_(UDX_CWND_INIT)
        , rid_(rid)
        , srtt_(0)
        , rttvar_(0)
        , rto_(0)
        , on_ms_(xq::tools::now_ms())
        , rcv_nxt_(0)
        , snd_nxt_(0)
    {}


    void _update_una(uint64_t una) {
        auto end = inf_buf_.find(una);
        if (end != inf_buf_.end()) {
            inf_buf_.erase(inf_buf_.begin(), end);
        }
    }


    int _update_push(Segment* new_seg) {
        uint64_t sn = new_seg->sn;
        int ret = 0;

        do {
            if (sn >= rcv_nxt_ + UDX_RCV_WND) {
                std::printf("sn: %lu >= rcv_nxt[%lu] + 256", sn, rcv_nxt_);
                ret = -102;
                break;
            }

            auto ack_itr = ack_que_.find(sn - 1);
            if (ack_itr != ack_que_.end()) {
                ack_que_.erase(ack_itr);
            }

            ack_itr = ack_que_.find(sn + 1);
            if (ack_itr == ack_que_.end()) {
                ack_que_[sn] = new_seg->ts;
            }

            if (sn < rcv_nxt_) {
                ret = 0;
                break;
            }

            auto itr = rcv_buf_.find(sn);
            if (itr == rcv_buf_.end()) {
                rcv_buf_.insert(std::make_pair(sn, new_seg));
                new_seg = nullptr;
            }
        } while (0);

        if (new_seg) {
            delete new_seg;
        }

        return ret;
    }


    int _update_ack(const Segment* seg, int64_t cur_ts) {
        int ret = 0;

        do {
            auto itr = inf_buf_.find(seg->sn);
            if (itr != inf_buf_.end()) {
                inf_buf_.erase(itr);
            }

            int32_t rtt = cur_ts - seg->ts;
            if (rtt < 0) {
                ret = -1;
                break;
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
            rto_ = xq::tools::MID(UDX_MIN_RTO, tmp, UDX_MAX_RTO);
            std::printf("> SN: %lu, RTT: %u, SRTT: %u, RTO: %u --------------- inf_buf: %lu\n", seg->sn, rtt, srtt_, rto_, inf_buf_.size());
        } while (0);

        if (seg) {
            delete seg;
        }

        return ret;
    }


    int _update_ping(const Segment* seg) {
        int ret = 0;

        do {

        } while (0);

        if (seg) {
            delete seg;
        }

        return ret;
    }


    int _update_pong(const Segment* seg) {
        int ret = 0;

        do {

        } while (0);

        if (seg) {
            delete seg;
        }

        return ret;
    }


    int _update_on(const Segment* seg) {
        int ret = 0;

        do {

        } while (0);

        if (seg) {
            delete seg;
        }

        return ret;
    }


    int _update_off(const Segment* seg) {
        int ret = 0;

        do {

        } while (0);

        if (seg) {
            delete seg;
        }

        return ret;
    }

    UdpSession::Ptr sess_;
    sockaddr addr_;
    socklen_t addrlen_;

    uint16_t cwnd_;
    uint32_t rid_, srtt_, rttvar_, rto_;
    uint64_t on_ms_;
    uint64_t rcv_nxt_, snd_nxt_;

    std::map<uint64_t, uint64_t> ack_que_;
    std::map<uint64_t, Segment*> rcv_buf_;
    std::map<uint64_t, Segment*> snd_buf_;
    std::map<uint64_t, Segment*> inf_buf_;
}; // class Xdg;


} // namespace net;
} // namespace xq;

#endif // !__XQ_NET_XDG__
