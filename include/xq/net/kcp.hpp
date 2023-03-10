#ifndef __XQ_NET_KCP__
#define __XQ_NET_KCP__


#include <map>
#include <list>
#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"


namespace xq {
namespace net {


// ------------------------------------------------------------------------ Kcp ------------------------------------------------------------------------

constexpr int KCP_WND           = 512;                     // KCP 默认读/写窗口
constexpr int KCP_MTU           = 1448;                    // KCP TODO: 目前为1448, KCP_MSS为 1448 - 24 = 1424, 未来消息头应该是: 36字节, KCP_MSS: 为1408 > 1392, 1392是因为要给一个PADDING16字节
constexpr int KCP_HEAD_SIZE     = 24;                      // KCP 消息头长度
constexpr int KCP_MSS           = KCP_MTU - KCP_HEAD_SIZE;
constexpr int KCP_MAX_DATA_SIZE = KCP_MSS * 128;           // KCP 单包最大字节
constexpr int KCP_TIMEOUT       = 60000;                   // KCP 默认超时(毫秒)
constexpr int KCP_UPDATE_MS     = 10;                      // KCP UPDATE 间隔(毫秒)
constexpr int KCP_RTO_MIN       = 100;    // normal min rto
constexpr int KCP_RTO_DEF       = 200;
constexpr int KCP_RTO_MAX       = 60000;
constexpr int KCP_CMD_PUSH      = 81;     // cmd: push data
constexpr int KCP_CMD_ACK       = 82;     // cmd: ack
constexpr int KCP_CMD_WASK      = 83;     // cmd: window probe (ask)
constexpr int KCP_CMD_WINS      = 84;     // cmd: window size (tell)
constexpr int KCP_ASK_SEND      = 1;      // need to send IKCP_CMD_WASK
constexpr int KCP_ASK_TELL      = 2;      // need to send IKCP_CMD_WINS
constexpr int KCP_MAX_SEG       = 128;    // must >= max fragment size
constexpr int KCP_ACK_FAST      = 3;
constexpr int KCP_DEADLINK      = 20;
constexpr int KCP_THRESH_INIT   = 2;
constexpr int KCP_THRESH_MIN    = 2;
constexpr int KCP_PROBE_INIT    = 7000;   // 7 secs to probe window size
constexpr int KCP_PROBE_LIMIT   = 120000; // up to 120 secs to probe window
constexpr int KCP_FASTACK_LIMIT = 5;      // max times to trigger fastack


/// @brief KCP协议 C++封装
class Kcp final {
public:
    struct Segment {

        static xq::tools::ObjectPool<Segment>* pool() {
            return xq::tools::ObjectPool<Segment>::instance();
        }

        uint32_t conv;
        uint8_t  cmd;
        uint8_t  frg;
        uint16_t wnd;
        uint32_t ts;
        uint32_t sn;
        uint32_t una;
        uint32_t len;
        uint32_t resendts;
        uint32_t rto;
        uint32_t fastack;
        uint32_t xmit;
        uint8_t  data[KCP_MSS];

        Segment() {
            ::memset(this, 0, sizeof(Segment));
        }

        std::string to_string() {
            char buff[3000];
            sprintf(buff, "CONV[%d]:CMD[%d]:FRG[%d]:WND[%d]:TS[%d]:SN[%d]:UNA[%d]:LEN[%d]:%s", conv, cmd, frg, wnd, ts, sn, una, len, xq::tools::bin2hex(data, len).c_str());
            return buff;
        }
    };


    typedef std::pair<uint64_t, uint64_t> Ack;


    /// @brief 构建函数
    /// @param conv kcp conv
    /// @param user 附加参数, 该框架中为: KcpSess / KcpHost
    Kcp(uint32_t conv, void* user, int (*output)(const uint8_t* buf, size_t len, void* user))
        : state_(0)
        , conv_(conv)
        , snd_una_(0)
        , snd_nxt_(0)
        , rcv_nxt_(0)
        , ssthresh_(KCP_THRESH_INIT)
        , rx_rttval_(0)
        , rx_srtt_(0)
        , rx_rto_(KCP_RTO_DEF)
        , snd_wnd_(KCP_WND)
        , rcv_wnd_(KCP_WND)
        , rmt_wnd_(1)
        , cwnd_(1)
        , probe_(0)
        , current_(0)
        , interval_(KCP_UPDATE_MS)
        , ts_flush_(KCP_UPDATE_MS)
        , xmit_(0)
        , nodelay_(1)
        , updated_(0)
        , ts_probe_(0)
        , probe_wait_(0)
        , incr_(0)
        , last_rcv_ts_(0)
        , last_snd_ts_(0)
        , user_(user)
        , buffer_(new uint8_t[KCP_MTU * 2])
        , nocwnd_(0)
        , output_(nullptr) {
        output_ = output;
    }


    ~Kcp() {
        Segment* seg;
        for (auto& itr : snd_buf_) {
            seg = itr.second;
            Segment::pool()->put(seg);
        }
        snd_buf_.clear();

        for (auto &itr : rcv_buf_) {
            seg = itr.second;
            Segment::pool()->put(seg);
        }
        rcv_buf_.clear();

        for (auto itr : snd_que_) {
            Segment::pool()->put(itr);
        }
        snd_que_.clear();

        for (auto itr : rcv_que_) {
            Segment::pool()->put(itr);
        }
        rcv_que_.clear();
        
        if (buffer_) {
            delete[] buffer_;
        }

        acklist_.clear();
    }


    /// @brief 从原始IO流中获取conv
    /// @param raw 原始IO数据
    /// @return 返回原始IO数据中的 conv
    static uint32_t get_conv(const void* raw) {
        uint32_t conv;
        _decode32u((const uint8_t*)raw, &conv);
        return conv;
    }


    static int decode(const uint8_t* data, size_t datalen, Segment* seg) {
        if (datalen < KCP_HEAD_SIZE) {
            return -1;
        }

        const uint8_t* p = data;
        p = _decode32u(p, &seg->conv);
        p = _decode8u(p, &seg->cmd);
        p = _decode8u(p, &seg->frg);
        p = _decode16u(p, &seg->wnd);
        p = _decode32u(p, &seg->ts);
        p = _decode32u(p, &seg->sn);
        p = _decode32u(p, &seg->una);
        p = _decode32u(p, &seg->len);

        ::memcpy(seg->data, p, seg->len);
        return KCP_HEAD_SIZE + seg->len;
    }


    /// @brief 获取当前Kcp conv
    uint32_t conv() const {
        return conv_;
    }


    void bad() {
        state_ = -1;
    }


    int state() const {
        return state_;
    }


    /// @brief 从kcp rcv_que 中接收数据
    /// @param buf  OUT数据缓冲区
    /// @param len  数据缓冲区长度
    /// @return 成功返回0, 否则返回!0
    int recv(uint8_t* buf, size_t len) {
        assert(buf);

        Segment* seg;
        int recover = 0;

        if (rcv_que_.empty()) {
            return -1;
        }

        int peeksize = _peeksize();

        if (peeksize < 0) {
            return -2;
        }

        if (peeksize > (int)len) {
            return -3;
        }

        if (rcv_que_.size() >= rcv_wnd_) {
            recover = 1;
        }

        // merge fragment
        len = 0;
        for (auto itr = rcv_que_.begin(); itr != rcv_que_.end(); ) {
            seg = *itr;
             
            memcpy(buf, seg->data, seg->len);
            buf += seg->len;

            len += seg->len;
            int fragment = seg->frg;

            rcv_que_.erase(itr++);
            Segment::pool()->put(seg);

            if (fragment == 0) {
                break;
            }
        }

        assert((int)len == peeksize);

        // move available data from rcv_buf -> rcv_queue
        while (!rcv_buf_.empty()) {
            auto itr = rcv_buf_.begin();
            seg = itr->second;
            if (seg->sn == rcv_nxt_ && rcv_que_.size() < rcv_wnd_) {
                rcv_buf_.erase(itr);
                rcv_que_.emplace_back(seg);
                rcv_nxt_++;
            }
            else {
                break;
            }
        }

        // fast recover
        if (rcv_que_.size() < rcv_wnd_ && recover) {
            // ready to send back IKCP_CMD_WINS in ikcp_flush
            // tell remote my window size
            probe_ |= KCP_ASK_TELL;
        }

        return len;
    }


    /// @brief 发送数据, 该发送仅把数据放入kcp发送队列
    /// @param buf 
    /// @param len 
    /// @return 成功返回0, 否则返回!0
    int send(const uint8_t* buf, size_t len) {
        Segment* seg;

        assert(buf && len > 0);

        // 1, 计算拆包数
        int count = (len <= KCP_MSS) ? 1 : (len + KCP_MSS - 1) / KCP_MSS;

        // 2, 判断是否大于最大分组
        if (count >= KCP_MAX_SEG) {
            // 超过最多分段时返回 -1
            return -1;
        }

        // fragment
        for (int i = 0; i < count; i++) {
            int size = len > KCP_MSS ? KCP_MSS : len;
            seg = Segment::pool()->get();
            assert(seg);

            memcpy(seg->data, buf, size);
            seg->len = size;
            seg->frg = count - i - 1;
            snd_que_.emplace_back(seg);
            buf += size;
            len -= size;
        }

        return 0;
    }


    /// @brief kcp update
    /// @param current 当前kcp时间(毫秒)
    int update(uint32_t now_ts) {
        if (state_ == -1) {
            return -1;
        }

        if ((int64_t)now_ts - last_rcv_ts_ > KCP_TIMEOUT) {
            return -2;
        }

        current_ = now_ts;

        if (updated_ == 0) {
            updated_ = 1;
            ts_flush_ = current_;
        }

        int32_t slap = current_ - ts_flush_;

        if (slap >= 10000 || slap < -10000) {
            ts_flush_ = current_;
            slap = 0;
        }

        if (slap >= 0) {
            if (current_ >= ts_flush_) {
                ts_flush_ = current_ + interval_;
            }
            else {
                ts_flush_ += interval_;
            }
            return flush();
        }

        return 0;
    }


    /// @brief 将原始IO流转换为kcp数据
    /// @param data 原始IO数据
    /// @param size 原始数据长度
    /// @return 成功返回0, 否则返回!0
    int input(const uint8_t* data, size_t size, int64_t now_ts) {
        uint32_t prev_una = snd_una_;
        uint32_t maxack = 0;
        int flag = 0;

        assert(data && size >= KCP_HEAD_SIZE);

        while (size > 0) {
            uint32_t ts, sn, len, una, conv;
            uint16_t wnd;
            uint8_t cmd, frg;
            Segment* seg;

            if (size < KCP_HEAD_SIZE) {
                return -2;
            }

            // Step 1: 解析消息头
            data = _decode32u(data, &conv);
            if (conv != conv_) {
                // 如果该消息并非来自于当前kcp
                return -1;
            }

            data = _decode8u(data, &cmd);
            data = _decode8u(data, &frg);
            data = _decode16u(data, &wnd);
            data = _decode32u(data, &ts);
            data = _decode32u(data, &sn);
            data = _decode32u(data, &una);
            data = _decode32u(data, &len);

            size -= KCP_HEAD_SIZE;

            if (size < len || len > 89 * 16 /* TODO: 后期需要修改, 主要原因是为了进行[AES128]加密 */) {
                // 当原始数据长度 < 实际数据长度, 实际数据长度 大于段最大长度
                return -2;
            }

            if (cmd < KCP_CMD_PUSH && cmd > KCP_CMD_WINS) {
                // 无效的CMD
                return -3;
            }

            // Step 2: 设置对端窗口
            rmt_wnd_ = wnd;
            // Step 3: 将seg::sn <= una 的包移出snd_buf
            _parse_una(una);
            // Step 4: 确认kcp->snd_una, 确保该值一定是snd_buf中的最小值
            _shrink_buf();

            switch (cmd) {
            case KCP_CMD_ACK: {
                if (current_ >= ts) {
                    // 如果当前时间 >= 分组的发送时间, 重新计算RTO
                    _update_ack(current_ - ts);
                }
                // 将ACK对应的分组移出snd_buf
                _parse_ack(sn);
                // 重新确认kcp->snd_una.
                _shrink_buf();
                if (flag == 0) {
                    flag = 1;
                    maxack = sn;
                }
                else if (sn > maxack) {
                    maxack = sn;
                }
            } break;

            case KCP_CMD_PUSH: {
                if (sn < rcv_nxt_ + rcv_wnd_) {
                    acklist_.emplace_back(std::make_pair(sn, ts));
                    if (sn >= rcv_nxt_) {
                        seg = Segment::pool()->get();
                        assert(seg);
                        seg->conv = conv_;
                        seg->cmd = cmd;
                        seg->frg = frg;
                        seg->wnd = wnd;
                        seg->ts = ts;
                        seg->sn = sn;
                        seg->una = una;
                        seg->len = len;

                        if (len > 0) {
                            memcpy(seg->data, data, len);
                        }

                        _parse_data(seg);
                    }
                }
            } break;

            case KCP_CMD_WASK: {
                probe_ |= KCP_ASK_TELL;
            } break;

            case KCP_CMD_WINS: {
                // do nothing
            } break;

            default:
                return -3;
            }

            data += len;
            size -= len;
        }

        if (flag) {
            // 检查快速重传
            _parse_fastack(maxack);
        }

        if (snd_una_ > prev_una) {
            // 重新计算cwnd
            if (cwnd_ < rmt_wnd_) {
                if (cwnd_ < ssthresh_) {
                    cwnd_++;
                    incr_ += KCP_MSS;
                }
                else {
                    if (incr_ < KCP_MSS) {
                        incr_ = KCP_MSS;
                    }
                    incr_ += (KCP_MSS * KCP_MSS) / incr_ + (KCP_MSS / 16);
                    if ((cwnd_ + 1) * KCP_MSS <= incr_) {
                        cwnd_ = (incr_ + KCP_MSS - 1) / ((KCP_MSS > 0) ? KCP_MSS : 1);
                    }
                }

                if (cwnd_ > rmt_wnd_) {
                    cwnd_ = rmt_wnd_;
                    incr_ = rmt_wnd_ * KCP_MSS;
                }
            }
        }

        last_rcv_ts_ = now_ts;
        return 0;
    }


    int waitsnd() {
        return snd_buf_.size() + snd_que_.size();
    }


    /// @brief 将发送缓冲区的数据给 回调函数(output)处理
    int flush() {
        uint32_t now_ms = current_;
        uint8_t* buf = buffer_;
        uint8_t* ptr = buf;
        int size;
        uint32_t rtomin;
        int change = 0;
        int lost = 0;
        Segment seg;
        seg.conv = conv_;
        seg.wnd = (uint32_t)_wnd_unused();
        seg.una = rcv_nxt_;

        // flush acknowledges
        if (acklist_.size() > 0) {
            seg.cmd = KCP_CMD_ACK;

            for (auto &ack: acklist_) {
                size = (int)(ptr - buf);
                if (size + (int)KCP_HEAD_SIZE > (int)KCP_MTU) {
                    if (_output(buf, size) > 0) {
                        last_snd_ts_ = now_ms;
                    }
                    ptr = buf;
                }
                seg.sn = ack.first;
                seg.ts = ack.second;
                ptr = _encode_seg(ptr, &seg);
            }

            acklist_.clear();
        }

        // probe window size (if remote window size equals zero)
        if (rmt_wnd_ == 0) {
            if (probe_wait_ == 0) {
                probe_wait_ = KCP_PROBE_INIT;
                ts_probe_ = now_ms + probe_wait_;
            }
            else {
                if (now_ms >= ts_probe_) {
                    if (probe_wait_ < KCP_PROBE_INIT) {
                        probe_wait_ = KCP_PROBE_INIT;
                    }

                    probe_wait_ += probe_wait_ / 2;
                    if (probe_wait_ > KCP_PROBE_LIMIT) {
                        probe_wait_ = KCP_PROBE_LIMIT;
                    }

                    ts_probe_ = now_ms + probe_wait_;
                    probe_ |= KCP_ASK_SEND;
                }
            }
        }
        else {
            ts_probe_ = 0;
            probe_wait_ = 0;
        }

        // flush window probing commands
        if (probe_ & KCP_ASK_SEND) {
            seg.cmd = KCP_CMD_WASK;
            size = (int)(ptr - buf);
            if (size + (int)KCP_HEAD_SIZE > (int)KCP_MTU) {
                if (_output(buf, size) > 0) {
                    last_snd_ts_ = now_ms;
                }
                ptr = buf;
            }
            ptr = _encode_seg(ptr, &seg);
        }

        // flush window probing commands
        if (probe_ & KCP_ASK_TELL) {
            seg.cmd = KCP_CMD_WINS;
            size = (int)(ptr - buf);
            if (size + (int)KCP_HEAD_SIZE > (int)KCP_MTU) {
                if (_output(buf, size) > 0) {
                    last_snd_ts_ = now_ms;
                }
                ptr = buf;
            }
            ptr = _encode_seg(ptr, &seg);
        }

        probe_ = 0;

        // calculate window size
        uint32_t cwnd = _imin_(snd_wnd_, rmt_wnd_);
        if (nocwnd_ == 0) cwnd = _imin_(cwnd_, cwnd);

        // move data from snd_queue to snd_buf
        while (snd_nxt_ < snd_una_ + cwnd) {
            if (snd_que_.empty()) {
                break;
            }

            Segment* newseg = snd_que_.front();
            snd_que_.pop_front();

            newseg->conv = conv_;
            newseg->cmd = KCP_CMD_PUSH;
            newseg->wnd = seg.wnd;
            newseg->ts = now_ms;
            newseg->sn = snd_nxt_;
            newseg->una = rcv_nxt_;
            newseg->resendts = now_ms;
            newseg->rto = rx_rto_;
            newseg->fastack = 0;
            newseg->xmit = 0;
            snd_buf_.insert(std::make_pair(snd_nxt_++, newseg));
        }

        // calculate resent
        rtomin = (nodelay_ == 0) ? (rx_rto_ >> 3) : 0;

        // flush data segments
        for (auto & itr : snd_buf_) {
            Segment* segment = itr.second;
            int needsend = 0;
            if (segment->xmit == 0) {
                needsend = 1;
                segment->xmit++;
                segment->rto = rx_rto_;
                segment->resendts = now_ms + segment->rto + rtomin;
            }
            else if (now_ms >= segment->resendts) {
                needsend = 1;
                segment->xmit++;
                xmit_++;

                if (nodelay_ == 0) {
                    segment->rto += _imax_(segment->rto, (uint32_t)rx_rto_);
                }
                else {
                    int32_t step = (nodelay_ < 2) ? ((int32_t)(segment->rto)) : rx_rto_;
                    segment->rto += step / 2;
                }
                segment->resendts = now_ms + segment->rto;
                lost = 1;
            }
            else if (segment->fastack >= KCP_ACK_FAST) {
                if ((int)segment->xmit <= KCP_FASTACK_LIMIT) {
                    needsend = 1;
                    segment->xmit++;
                    segment->fastack = 0;
                    segment->resendts = now_ms + segment->rto;
                    change++;
                }
            }

            if (needsend) {
                int need;
                segment->ts = now_ms;
                segment->wnd = seg.wnd;
                segment->una = rcv_nxt_;

                size = (int)(ptr - buf);
                need = KCP_HEAD_SIZE + segment->len;

                if (size + need > (int)KCP_MTU) {
                    if (_output(buf, size) > 0) {
                        last_snd_ts_ = now_ms;
                    }
                    ptr = buf;
                }

                ptr = _encode_seg(ptr, segment);

                if (segment->len > 0) {
                    memcpy(ptr, segment->data, segment->len);
                    ptr += segment->len;
                }

                if (segment->xmit >= KCP_DEADLINK) {
                    assert(0 && "################# XMIT ################");
                    state_ = -1;
                    return -1;
                }
            }
        }

        // flash remain segments
        size = (int)(ptr - buf);
        if (size > 0) {
            if (_output(buf, size) > 0) {
                last_snd_ts_ = now_ms;
            }
        }

        // update ssthresh
        if (change) {
            uint32_t inflight = snd_nxt_ - snd_una_;
            ssthresh_ = inflight / 2;
            if (ssthresh_ < KCP_THRESH_MIN) {
                ssthresh_ = KCP_THRESH_MIN;
            }
            cwnd_ = ssthresh_ + KCP_ACK_FAST;
            incr_ = cwnd * KCP_MSS;
        }

        if (lost) {
            ssthresh_ = cwnd / 2;
            if (ssthresh_ < KCP_THRESH_MIN) {
                ssthresh_ = KCP_THRESH_MIN;
            }
            cwnd_ = 1;
            incr_ = KCP_MSS;
        }

        if (cwnd_ < 1) {
            cwnd_ = 1;
            incr_ = KCP_MSS;
        }

        return 0;
    }


    /// @brief 重置KCP
    void reset(uint32_t conv) {
        Segment* seg;

        for (auto& itr : snd_buf_) {
            seg = itr.second;
            Segment::pool()->put(seg);
        }
        snd_buf_.clear();

        for (auto &itr : rcv_buf_) {
            seg = itr.second;
            Segment::pool()->put(seg);
        }
        rcv_buf_.clear();

        for (auto itr : snd_que_) {
            Segment::pool()->put(itr);
        }
        snd_que_.clear();

        for (auto itr : rcv_que_) {
            Segment::pool()->put(itr);
        }
        rcv_que_.clear();

        acklist_.clear();

        conv_ = conv;
        snd_una_ = 0;
        snd_nxt_ = 0;
        rcv_nxt_ = 0;
        ts_probe_ = 0;
        last_rcv_ts_ = 0;
        last_snd_ts_ = 0;
        probe_wait_ = 0;
        cwnd_ = 1;
        ssthresh_ = KCP_THRESH_INIT;
        incr_ = 0;
        probe_ = 0;
        state_ = 0;
        rx_srtt_ = 0;
        rx_rttval_ = 0;
        current_ = 0;
        nodelay_ = 0;
        updated_ = 0;
        nocwnd_ = 0;
        xmit_ = 0;
    }


    uint32_t check(uint32_t now_ms) {
        uint32_t ts_flush = ts_flush_;
        int32_t tm_flush = 0;
        int32_t tm_packet = 0x7fffffff;
        uint32_t minimal = 0;

        if (updated_ == 0) {
            return 0;
        }

        int32_t slap = (int32_t)(now_ms - ts_flush);

        if (slap >= 10000 || slap < -10000) {
            ts_flush = now_ms;
        }

        if (now_ms >= ts_flush) {
            return 0;
        }

        tm_flush = -slap;

        for (auto &itr: snd_buf_) {
            const Segment* seg = itr.second;
            int32_t diff = (int32_t)(seg->resendts - now_ms);
            if (diff <= 0) {
                return 0;
            }
            else if (diff < tm_packet) {
                tm_packet = diff;
            }
        }

        minimal = (uint32_t)(tm_packet < tm_flush ? tm_packet : tm_flush);
        if (minimal >= interval_) {
            minimal = interval_;
        }

        return minimal;
    }


private:
    int _output(const uint8_t* data, size_t size) {
        assert(output_);
        if (size == 0) {
            return 0;
        }
        return output_(data, size, user_);
    }


    static uint8_t* _encode_seg(uint8_t* ptr, const Segment* seg) {
        ptr = _encode32u(ptr, seg->conv);
        ptr = _encode8u(ptr, (uint8_t)seg->cmd);
        ptr = _encode8u(ptr, (uint8_t)seg->frg);
        ptr = _encode16u(ptr, (uint16_t)seg->wnd);
        ptr = _encode32u(ptr, seg->ts);
        ptr = _encode32u(ptr, seg->sn);
        ptr = _encode32u(ptr, seg->una);
        ptr = _encode32u(ptr, seg->len);
        return ptr;
    }


    static uint8_t* _encode8u(uint8_t* p, uint8_t c) {
        *p++ = c;
        return p;
    }


    static const uint8_t* _decode8u(const uint8_t* p, uint8_t* c) {
        *c = *p++;
        return p;
    }


    static uint8_t* _encode16u(uint8_t* p, uint16_t w) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        *(uint8_t*)(p + 0) = (w & 0xff);
        *(uint8_t*)(p + 1) = (w >> 8);
#else
        memcpy(p, &w, 2);
#endif
        p += 2;
        return p;
    }


    static const uint8_t* _decode16u(const uint8_t* p, uint16_t* w) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        *w = *(const uint8_t*)(p + 1);
        *w = *(const uint8_t*)(p + 0) + (*w << 8);
#else
        memcpy(w, p, 2);
#endif
        p += 2;
        return p;
    }


    static uint8_t* _encode32u(uint8_t* p, uint32_t l) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        *(uint8_t*)(p + 0) = (uint8_t)(l);
        *(uint8_t*)(p + 1) = (uint8_t)((l >> 8));
        *(uint8_t*)(p + 2) = (uint8_t)((l >> 16));
        *(uint8_t*)(p + 3) = (uint8_t)((l >> 24));
#else
        memcpy(p, &l, 4);
#endif
        p += 4;
        return p;
    }


    static const uint8_t* _decode32u(const uint8_t* p, uint32_t* l) {
#if X_BIG_ENDIAN || X_MUST_ALIGN
        *l = *(const uint8_t*)(p + 3);
        *l = *(const uint8_t*)(p + 2) + (*l << 8);
        *l = *(const uint8_t*)(p + 1) + (*l << 8);
        *l = *(const uint8_t*)(p + 0) + (*l << 8);
#else 
        memcpy(l, p, 4);
#endif
        p += 4;
        return p;
    }


    static uint32_t _imin_(uint32_t a, uint32_t b) {
        return a <= b ? a : b;
    }

    static uint32_t _imax_(uint32_t a, uint32_t b) {
        return a >= b ? a : b;
    }

    static uint32_t _ibound_(uint32_t lower, uint32_t middle, uint32_t upper)
    {
        return _imin_(_imax_(lower, middle), upper);
    }


    int _wnd_unused() {
        return rcv_que_.size() < rcv_wnd_ ? rcv_wnd_ - rcv_que_.size() : 0;
    }


    int _wndsize(int sndwnd, int rcvwnd) {
        if (sndwnd > 0) {
            snd_wnd_ = sndwnd;
        }
        if (rcvwnd > 0) {   // must >= max fragment size
            rcv_wnd_ = _imax_(rcvwnd, KCP_MAX_SEG);
        }
        return 0;
    }


    void _parse_una(uint32_t una) {
        auto end = snd_buf_.find(una);
        for (auto itr = snd_buf_.begin(); itr != end;) {
            Segment::pool()->put(itr->second);
            snd_buf_.erase(itr++);
        }
    }


    // 该函数确保kcp->snd_una为snd_buf中的最小值, 并且每当snd_buf队列前端的数据有变动时, 都需要调用该函数来确认snd_una
    void _shrink_buf() {
        if (snd_buf_.empty()) {
            // 如果发送缓冲区为空 snd_una 为下次要发送分组的sn
            snd_una_ = snd_nxt_;
        }
        else {
            // 如果发送缓冲区不为空 snd_una 为发送缓冲区中最小数分组的sn
            snd_una_ = (*snd_buf_.begin()).second->sn;
        }
    }


    void _update_ack(int32_t rtt) {
        int32_t rto = 0;
        if (rx_srtt_ == 0) {
            rx_srtt_ = rtt;
            rx_rttval_ = rtt / 2;
        }
        else {
            long delta = rtt - rx_srtt_;
            if (delta < 0) delta = -delta;
            rx_rttval_ = (3 * rx_rttval_ + delta) / 4;
            rx_srtt_ = (7 * rx_srtt_ + rtt) / 8;
            if (rx_srtt_ < 1) rx_srtt_ = 1;
        }
        rto = rx_srtt_ + _imax_(interval_, 4 * rx_rttval_);
        rx_rto_ = _ibound_(KCP_RTO_MIN, rto, KCP_RTO_MAX);
    }


    void _parse_ack(uint32_t sn) {
        if (sn < snd_una_ || sn >= snd_nxt_) {
            return;
        }

        auto itr = snd_buf_.find(sn);
        if (itr != snd_buf_.end()) {
            snd_buf_.erase(itr);
        }
    }


    void _parse_data(Segment* newseg) {
        uint32_t sn = newseg->sn;

        if (sn >= rcv_nxt_ + rcv_wnd_ || sn < rcv_nxt_) {
            Segment::pool()->put(newseg);
            return;
        }

        auto itr = rcv_buf_.find(sn);
        if (itr == rcv_buf_.end()) {
            rcv_buf_.insert(std::make_pair(newseg->sn, newseg));
        }
        else {
            Segment::pool()->put(newseg);
        }

        // move available data from rcv_buf -> rcv_queue
        while (!rcv_buf_.empty()) {
            auto itr = rcv_buf_.begin();
            Segment* seg = itr->second;
            if (seg->sn == rcv_nxt_ && rcv_que_.size() < rcv_wnd_) {
                rcv_buf_.erase(itr);
                rcv_que_.emplace_back(seg);
                rcv_nxt_++;
            }
            else {
                break;
            }
        }
    }


    // 确认 缓冲区中分组 sn 被跨越了多少次
    void _parse_fastack(uint32_t sn) {
        if (sn < snd_una_ || sn >= snd_nxt_)
            return;

        for (auto &itr: snd_buf_) {
            Segment* seg = itr.second;
            if (sn < seg->sn) {
                break;
            }
            else if (sn != seg->sn) {
                seg->fastack++;
            }
        }
    }


    int _peeksize() {
        Segment* seg;
        int length = 0;

        if (rcv_que_.empty()) {
            return -1;
        }

        seg = rcv_que_.front();
        if (seg->frg == 0) {
            return seg->len;
        }

        if (rcv_que_.size() < (size_t)(seg->frg + 1)) {
            return -1;
        }

        for (auto itr: rcv_que_) {
            seg = itr;
            length += seg->len;
            if (seg->frg == 0) {
                break;
            }
        }

        return length;
    }


    int state_;
    uint32_t conv_;
    uint32_t snd_una_, snd_nxt_, rcv_nxt_;
    uint32_t ssthresh_;
    int32_t rx_rttval_, rx_srtt_, rx_rto_;
    uint32_t snd_wnd_, rcv_wnd_, rmt_wnd_, cwnd_, probe_;
    uint32_t current_, interval_, ts_flush_, xmit_;
    uint32_t nodelay_, updated_;
    uint32_t ts_probe_, probe_wait_;
    uint32_t incr_;
    int64_t last_rcv_ts_, last_snd_ts_;
    std::list<Segment*> snd_que_;
    std::list<Segment*> rcv_que_;
    std::map<uint32_t, Segment*> snd_buf_;
    std::map<uint32_t, Segment*> rcv_buf_;
    std::vector<Ack> acklist_;
    void* user_;
    uint8_t* buffer_;
    int nocwnd_;
    int (*output_)(const uint8_t* buf, size_t len, void* user);


    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
}; // class Kcp


} // namespace net
} // namespace xq


#endif // !__XQ_NET_KCP__
