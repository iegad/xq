#ifndef __XQ_NET_COMMON__
#define __XQ_NET_COMMON__


#include <atomic>
#include <regex>
#include <thread>
#include <list>
#include <map>
#include "xq/net/rux.in.h"


namespace xq {
namespace net {


/* common */
constexpr int           IPV4_HDR_SIZE           = 20;                                                           // IPv4 Header size
constexpr int           IPV6_HDR_SIZE           = 40;                                                           // IPv6 Header size
constexpr int           UDP_HDR_SIZE            = 8;                                                            // UDP Header size
constexpr int           ETH_FRM_SIZE            = 1500;                                                         // Ethernet payload size
constexpr int           RUX_MTU                 = ETH_FRM_SIZE - UDP_HDR_SIZE - IPV6_HDR_SIZE;                  // RUX Maximum Transmission Unit: 1452

/* rux property */
constexpr int           RUX_FRM_HDR_SIZE        = 10;                                                           // RUX Frame Header size: rid[3] + wnd[1]
constexpr int           RUX_SEG_HDR_SIZE        = 13;                                                           // RUX Segment Header size: cmd[1] + sn[6] + us[6]
constexpr int           RUX_SEG_HDR_EX_SIZE     = RUX_SEG_HDR_SIZE + 3;                                         // RUX Segment Header extension size: RUX_SEG_HDR_SIZE[13] + len[2] + frg[1]
constexpr int           RUX_MSS                 = (RUX_MTU - RUX_SEG_HDR_EX_SIZE - RUX_FRM_HDR_SIZE) / 16 * 16; // RUX Maximum segment size

/* rux command */
constexpr int           RUX_CMD_ACK             = 0x01;                                                         // ACK
constexpr int           RUX_CMD_PIN             = 0x03;                                                         // Heart's beat: Ping
constexpr int           RUX_CMD_CON             = 0x04;                                                         // Connection
constexpr int           RUX_CMD_PSH             = 0x05;                                                         // Push

/* rux limits */
constexpr int           RUX_RID_MAX             = 100000;                                                       // Maximum rux id
constexpr int           RUX_RWND_MAX            = 128;                                                          // Maximum receive window size
constexpr int           RUX_SWND_MAX            = RUX_RWND_MAX / 2;                                             // Maximum send window size
constexpr int           RUX_SWND_MIN            = 1;
constexpr uint64_t      RUX_SN_MAX              = 0x0000FFFFFFFFFFFF;                                           // Maximum sequnce number
constexpr uint64_t      RUX_US_MAX              = 0x0000FFFFFFFFFFFF;                                           // Maximum timestamp(us)
constexpr int           RUX_FRM_MAX             = 92;                                                           // Maximum fragment size
constexpr int           RUX_MSG_MAX             = RUX_FRM_MAX * RUX_MSS;                                        // Maximum signle massage's length
constexpr int           RUX_RTO_MIN             = 200 * 1000;                                                    // RTO MIN 200ms
constexpr int           RUX_RTO_MAX             = 1000 * 1000 * 30;                                             // RTO MAX 30s
constexpr int           RUX_TIMEOUT             = RUX_RTO_MAX * 2 * 10;                                         // TIMEOUT: 10 min
constexpr int           RUX_FAST_ACK            = 3;
constexpr int           RUX_XMIT_MAX            = 20;
constexpr int           RUX_SSTHRESH_INIT       = 8;

/* error */
enum ErrType {
    IO_RCV,
    IO_RCV_FRAME,
    IO_SND,
    RUX_INPUT,
    RUX_OUTPUT
};


// IPv4 regex
constexpr char REG_IPV4[] = "^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$";
// IPv6 regex
constexpr char REG_IPV6[] = "^\\[(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\\]$";


// ===============================================================================================
// 判断 ip 类型
// ===============================================================================================
inline int get_ip_type(const std::string& ip) {
    static const std::regex REG_V6(REG_IPV6);
    static const std::regex REG_V4(REG_IPV4);

    if (std::regex_match(ip, REG_V6)) {
        return AF_INET6;
    }

    if (std::regex_match(ip, REG_V4)) {
        return AF_INET;
    }

    return -1;
}


// ###############################################################################################
// Rux Frame
//     * 用于udp 通信. Rux在IO层收到消息以帧为单为;
//     * 帧长度不会超过以太网MTU(1508);
// ###############################################################################################
typedef struct __frame_ {
    uint16_t             len;                // raw data's length
    socklen_t            namelen;            // remote sockaddr's length
    sockaddr_storage     name;               // remote sockaddr
    uint8_t              raw[RUX_MTU + 1];   // raw data

    /* META */
    uint32_t             rid;                // RUX id
    uint8_t              wnd;                // receive window size
    uint64_t             una;                // wnd una
    int64_t              time_us;            // receive timestamp(us)
    void*                rux;


    // ===========================================================================================
    // 构造函数
    // ===========================================================================================
    __frame_()
        : len(0)
        , namelen(sizeof(sockaddr))
        , name({0,{0},0})
        , rid(0)
        , wnd(0)
        , una(0)
        , time_us(0)
        , rux(nullptr) {
        ::memset(raw, 0, RUX_MTU + 1);
    }

    
    ~__frame_() {
    }


    // ===========================================================================================
    // 检查帧的合法性
    //      成功返回 0, 否则返回 -1;
    //      检查帧时会自动为帧打上 rux id 和 对端 接收窗口大小
    // ===========================================================================================
    int check() {
        if (len == 0 || len < RUX_FRM_HDR_SIZE || len > RUX_MTU) {
            return -1;
        }

        u24_decode(raw, &rid);
        if (rid == 0 || rid > RUX_RID_MAX) {
            return -2;
        }

        u8_decode(raw + 3, &wnd);
        if (wnd > RUX_RWND_MAX) {
            return -3;
        }

        u48_decode(raw + 4, &una);
        if (una > RUX_SN_MAX) {
            return -4;
        }

        return 0;
    }


    // ===========================================================================================
    // 装载帧, 将帧 META 属性中 rid 和 wnd 装载到 raw 原始码流中
    //      成功返回 0, 否则返回 -1;
    // ===========================================================================================
    int setup() {
        if (rid == 0 || rid > RUX_RID_MAX || wnd > RUX_RWND_MAX) {
            return -1;
        }

        u24_encode(rid, raw);
        u8_encode(wnd, raw + 3);
        u48_encode(una, raw + 4);
        return 0;
    }


private:
    __frame_(const __frame_&) = delete;
    __frame_(const __frame_&&) = delete;
    __frame_& operator=(const __frame_&) = delete;
    __frame_& operator=(const __frame_&&) = delete;
} RUX_FRM, *PRUX_FRM;


// ###############################################################################################
// Rux Segment
//     * Rux 分组, 用于承载消息.
// 
// 和 Frame 的关系是, 一个 Frame 可以包含多个 Segment, 但最少需要包含一个 Segment
// ###############################################################################################
typedef struct __segment_ {
    uint8_t             cmd;               // command
    uint64_t            sn;                // sequence number
    uint64_t            us;                // timestamp(us)
    uint16_t            len;               // segment's length
    uint8_t             frg;               // fragment
    uint8_t             data[RUX_MSS];     // data: 1424

    /* META */
    uint8_t             fastack;           //
    uint16_t            xmit;              //
    uint32_t            rto;               //
    uint64_t            resend_us;         
    uint32_t            rid;               // rux id
    int64_t             time_us;           // receive timestamp(us)
    sockaddr_storage*   addr;
    socklen_t           addrlen;


    // ===========================================================================================
    // 构造函数
    // ===========================================================================================
    __segment_()
        : cmd(0)
        , sn(0)
        , us(0)
        , len(0)
        , frg(0)
        , fastack(0)
        , xmit(0)
        , rto(0)
        , resend_us(0)
        , rid(0)
        , time_us(0)
        , addr(nullptr)
        , addrlen(sizeof(sockaddr_storage)) {
        ::memset(data, 0, RUX_MSS);
    }


    ~__segment_() {
    }


    // ===========================================================================================
    // 设置 data 数据
    //      最好使用该法来 copy 消息数据, 因为该方法会将 datalen 一并赋值.
    // ===========================================================================================
    void set_data(const uint8_t* data, int datalen) {
        ASSERT(data && datalen <= RUX_MTU);
        ::memcpy(this->data, data, datalen);
        len = datalen;
    }


    // ===========================================================================================
    // segment 编码
    //      成功返回 编码长度, 否则返回 -1
    // ===========================================================================================
    int encode(uint8_t* buf, int buflen) {
        if (buflen > RUX_MTU || buflen < RUX_SEG_HDR_SIZE) {
            DLOG("------------------>> buflen = %d\n", buflen);
            ::exit(1);
        }
        ASSERT(buf && buflen >= RUX_SEG_HDR_SIZE && buflen <= RUX_MTU);

        uint8_t* p = buf;
        if (cmd == 0) {
            return -1;
        }

        if (sn > RUX_SN_MAX) {
            return -1;
        }

        if (us > RUX_US_MAX) {
            return -1;
        }

        p += u8_encode(cmd, p);
        p += u48_encode(sn, p);
        p += u48_encode(us, p);

        if (cmd >= RUX_CMD_CON) {
            if (len > RUX_MSS) {
                return -1;
            }

            if (frg > RUX_FRM_MAX) {
                return -1;
            }

            p += u16_encode(len, p);
            p += u8_encode(frg, p);
            ::memcpy(p, data, len);
            p += len;
        }

        return p - buf;
    }


    // ===========================================================================================
    // segment 解码
    //      成功返回 0, 否则返回 -1
    // ===========================================================================================
    int decode(const uint8_t* buf, uint16_t buflen) {
        ASSERT(buf);
        if (buflen < RUX_SEG_HDR_SIZE) {
            return -1;
        }

        const uint8_t* p = buf;

        p += u8_decode(p, &cmd);
        if (cmd == 0 || cmd > RUX_CMD_PSH) {
            return -1;
        }

        p += u48_decode(p, &sn);
        if (sn > RUX_SN_MAX) {
            return -1;
        }

        p += u48_decode(p, &us);
        if (us > RUX_SN_MAX) {
            return -1;
        }

        if (cmd >= RUX_CMD_CON) {
            if (buflen < RUX_SEG_HDR_EX_SIZE) {
                return -1;
            }

            p += u16_decode(p, &len);
            if (len > RUX_MSS) {
                return -1;
            }

            p += u8_decode(p, &frg);
            if (frg > RUX_FRM_MAX) {
                return -1;
            }

            ::memcpy(data, p, len);
            p += len;
        }

        return p - buf;
    }


private:
    __segment_(const __segment_&) = delete;
    __segment_(const __segment_&&) = delete;
    __segment_& operator=(const __segment_&) = delete;
    __segment_& operator=(const __segment_&&) = delete;
} RUX_SEG, *PRUX_SEG;


// ###############################################################################################
// ACK 
//      用于 Rux CMD 中 ACK消息
// ###############################################################################################
typedef struct __ack_ {
    uint64_t sn;
    uint64_t us;


    __ack_() : sn(0), us(0) {}
    __ack_(uint64_t sn, uint64_t us) : sn(sn), us(us) {}


private:
    __ack_(const __ack_&) = delete;
    __ack_(const __ack_&&) = delete;
    __ack_& operator=(const __ack_&) = delete;
    __ack_& operator=(const __ack_&&) = delete;
} RUX_ACK, *PRUX_ACK;


// ###############################################################################################
// SpinLock 自旋锁
//      windows 下 使用 windows自带原子操作函数
//      linux   下 则使用 posix 自旋锁
// ###############################################################################################
#ifdef WIN32
typedef struct __spin_lock_ {
    void lock() {
        while (InterlockedCompareExchange(&m_, 1, 0) != 0) {
            _mm_pause();
        }
    }

    void unlock() {
        InterlockedExchange(&m_, 0);
    }


    __spin_lock_() : m_(0) {}

private:
    long m_;
} SPIN_LOCK, * PSPIN_LOCK;
#else
typedef struct __spin_lock_ {
    void lock() {
        ASSERT(!pthread_spin_lock(&m_));
    }

    void unlock() {
        ASSERT(!pthread_spin_unlock(&m_));
    }


    __spin_lock_() {
        ASSERT(!pthread_spin_init(&m_, PTHREAD_PROCESS_PRIVATE));
    }


    ~__spin_lock_() {
        ASSERT(!pthread_spin_destroy(&m_));
    }

private:
    pthread_spinlock_t m_;
} SPIN_LOCK, * PSPIN_LOCK;
#endif // WIN32


// ###############################################################################################
// 发送缓冲区(Lockfree)
// ###############################################################################################
typedef struct __snd_buf_ {
    __snd_buf_()
        : nsize_(0)
        , max_sn_(0)
        , snd_una_(0) {
        ::memset(buf_, 0, sizeof(PRUX_SEG) * MAX);
    }


    ~__snd_buf_() {
        clear();
    }


    int insert(const PRUX_SEG seg) {
        int pos = seg->sn % MAX;

        lkr_.lock();
        if (nsize_ >= MAX || buf_[pos]) {
            lkr_.unlock();
            return -1;
        }

        if (nsize_ > 0 && max_sn_ + 1 != seg->sn) {
            lkr_.unlock();
            return -1;
        }

        max_sn_ = seg->sn;
        buf_[pos] = seg;
        nsize_++;
        lkr_.unlock();
        return 0;
    }


    // ===========================================================================================
    // 更新 ack
    //      从 snd_buf_ 中删除对应的 Segment, 并将被跳过的 Segment 快重传标志递增 1.
    // ===========================================================================================
    void update_ack(uint64_t sn) {
        int pos = sn % MAX;
        lkr_.lock();

        if (sn >= snd_una_ && sn <= max_sn_ && buf_[pos]) {
            delete buf_[pos];
            buf_[pos] = nullptr;
            nsize_--;

            if (sn == snd_una_) {
                if (nsize_ > 0) {
                    for (uint64_t i = snd_una_, n = max_sn_; i <= n; i++) {
                        pos = i % MAX;
                        if (buf_[pos]) {
                            snd_una_ = buf_[pos]->sn;
                            break;
                        }
                    }
                }
                else {
                    snd_una_++;
                }
            }
        }

        lkr_.unlock();
    }


    void update_una(uint64_t una) {
        lkr_.lock();
        if (nsize_ > 0 && una <= max_sn_ + 1 && una > snd_una_) {

            int pos, n = 0;
            uint64_t i;
            
            for (i = snd_una_; i < una; i++) {
                pos = i % MAX;
                if (buf_[pos]) {
                    delete buf_[pos];
                    buf_[pos] = nullptr;
                    n++;
                }
            }

            nsize_ -= n;
            snd_una_ += n;

            if (nsize_ > 0) {
                for (; i <= max_sn_; i++) {
                    pos = i % MAX;
                    if (buf_[pos]) {
                        snd_una_ = buf_[pos]->sn;
                        break;
                    }
                }
            }
        }
        lkr_.unlock();
    }


    void clear() {
        int pos;
        lkr_.lock();

        if (nsize_ > 0) {
            for (uint64_t beg = snd_una_; beg <= max_sn_; beg++) {
                pos = beg % MAX;
                if (buf_[pos]) {
                    delete buf_[pos];
                    buf_[pos] = nullptr;
                }
            }
        }
        else {
            nsize_ = 0;
        }

        lkr_.unlock();
    }


    size_t size() {
        lkr_.lock();
        size_t n = nsize_;
        lkr_.unlock();
        return n;
    }


    void get_segs(std::list<PRUX_SEG> *segs, size_t n, uint64_t now_us) {
        PRUX_SEG seg;
        int pos;
        lkr_.lock();
        if (nsize_ > 0) {
            for (uint64_t beg = snd_una_; beg <= max_sn_; beg++) {
                if (segs->size() == n) {
                    break;
                }

                pos = beg % MAX;
                seg = buf_[pos];
                if (seg) {
                    if (seg->xmit == 0) {
                        segs->emplace_back(seg);
                    }
                    else {
                        if (seg->resend_us <= now_us || seg->fastack >= RUX_FAST_ACK) {
                            segs->emplace_back(seg);
                        }
                        else if (seg->sn == 0) {
                            break;
                        }
                    }
                }
            }
        }
        lkr_.unlock();
    }


private:
    static constexpr int MAX = RUX_SWND_MAX * 1.5;


    int         nsize_;     // 发送缓冲区大小
    uint64_t    max_sn_;    // 发送缓冲区最大SN
    uint64_t    snd_una_;   // 发送窗口
    SPIN_LOCK   lkr_;       
    PRUX_SEG    buf_[MAX];  
} RUX_SBUF, *PRUX_SBUF;


// ###############################################################################################
// Ack 队列(Lockfree)
// ###############################################################################################
typedef struct __ack_que_ {
    void clear() {
        std::list<PRUX_ACK>::iterator itr;
        lkr_.lock();
        while (que_.size() > 0) {
            itr = que_.begin();
            delete* itr;
            que_.erase(itr);
        }
        lkr_.unlock();
    }


    void insert(uint64_t sn, uint64_t us) {
        PRUX_ACK ack = new RUX_ACK(sn, us);
        lkr_.lock();
        que_.emplace_back(ack);
        lkr_.unlock();
    }


    size_t size() {
        lkr_.lock();
        size_t n = que_.size();
        lkr_.unlock();
        return n;
    }


    void move_in(std::list<PRUX_ACK>* ack_list) {
        std::list<PRUX_ACK>::iterator itr;
        lkr_.lock();
        if (que_.size() > 0) {
            *ack_list = std::move(que_);
        }
        lkr_.unlock();
    }


private:
    std::list<PRUX_ACK> que_;
    SPIN_LOCK lkr_;
} RUX_ACKQ, *PRUX_ACKQ;


// ###############################################################################################
// 接收缓冲区
// ###############################################################################################
typedef struct __rcv_buf_ {
    __rcv_buf_() : nsize_(0) {
        ::memset(buf_, 0, MAX * sizeof(PRUX_SEG));
    }


    ~__rcv_buf_() {
        clear();
    }


    inline int size() const {
        return nsize_;
    }


    void clear() {
        if (nsize_ > 0) {
            nsize_ = 0;
            for (int i = 0; i < MAX; i++) {
                if (buf_[i]) {
                    delete buf_[i];
                    buf_[i] = nullptr;
                }
            }
        }
    }


    int get_msg(uint8_t *msg, uint64_t *rcv_nxt) {
        if (nsize_ == 0) {
            return 0;
        }

        int pos, n = 0, ok = 0;
        uint64_t nxt = *rcv_nxt, save = nxt;
        PRUX_SEG segs[RUX_FRM_MAX];
        PRUX_SEG seg;

        for (uint64_t beg = save, end = save + nsize_; beg <= end; beg++) {
            pos = (int)(beg % MAX);
            seg = buf_[pos];
            if (!seg) {
                return 0;
            }

            nxt++;
            segs[n++] = seg;
            if (seg->frg == 0) {
                ok = 1;
                break;
            }
        }

        if (ok) {
            uint8_t* p = msg;
            for (int i = 0; i < n; i++) {
                seg = segs[i];
                ::memcpy(p, seg->data, seg->len);
                p += seg->len;
            }

            for (uint64_t beg = save; beg < nxt; beg++) {
                pos = (int)(beg % MAX);
                delete buf_[pos];
                buf_[pos] = nullptr;
            }

            nsize_ -= n;
            *rcv_nxt = nxt;
            return p - msg;
        }

        return 0;
    }


    inline void insert(PRUX_SEG seg) {
        buf_[seg->sn % MAX] = seg;
        nsize_++;
    }


    inline int count(uint64_t sn) {
        return buf_[sn % MAX] == nullptr ? 0 : 1;
    }


private:
    static constexpr int MAX = RUX_RWND_MAX * 1.1;


    int         nsize_;     // 接收缓冲区大小
    PRUX_SEG    buf_[MAX];  // 接收缓冲区
} RUX_RBUF, *PRUX_RBUF;


} // namespace net
} // namespace xq


#endif // __XQ_NET_COMMON__
