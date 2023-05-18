#ifndef __XQ_NET_COMMON__
#define __XQ_NET_COMMON__


#include <regex>
#include "rux.in.h"


namespace xq {
namespace net {


/* common */
constexpr int   IPV4_HDR_SIZE           = 20;                                                           // IPv4 Header size
constexpr int   IPV6_HDR_SIZE           = 40;                                                           // IPv6 Header size
constexpr int   UDP_HDR_SIZE            = 8;                                                            // UDP Header size
constexpr int   ETH_FRM_SIZE            = 1500;                                                         // Ethernet payload size
constexpr int   RUX_MTU                 = ETH_FRM_SIZE - UDP_HDR_SIZE - IPV6_HDR_SIZE;                  // RUX Maximum Transmission Unit: 1452

/* rux property */
constexpr int   RUX_FRM_HDR_SIZE        = 4;                                                            // RUX Frame Header size: rid[3] + wnd[1]
constexpr int   RUX_SEG_HDR_SIZE        = 13;                                                           // RUX Segment Header size: cmd[1] + sn[6] + us[6]
constexpr int   RUX_SEG_HDR_EX_SIZE     = RUX_SEG_HDR_SIZE + 3;                                         // RUX Segment Header extension size: RUX_SEG_HDR_SIZE[13] + len[2] + frg[1]
constexpr int   RUX_MSS                 = (RUX_MTU - RUX_SEG_HDR_EX_SIZE - RUX_FRM_HDR_SIZE) / 16 * 16; // RUX Maximum segment size

/* rux command */
constexpr int   RUX_CMD_ACK             = 0x01;                                                         // ACK
constexpr int   RUX_CMD_PON             = 0x02;                                                         // Heart's beat: Pong
constexpr int   RUX_CMD_PIN             = 0x03;                                                         // Heart's beat: Ping
constexpr int   RUX_CMD_CON             = 0x04;                                                         // Connection
constexpr int   RUX_CMD_PSH             = 0x05;                                                         // Push

/* rux limits */
constexpr int   RUX_RID_MAX             = 100000;                                                       // Maximum rux id
constexpr int   RUX_RWND_MAX            = 128;                                                          // Maximum receive window size
constexpr int   RUX_SWND_MAX            = RUX_RWND_MAX / 4;                                             // Maximum send window size
constexpr int   RUX_SWND_MIN            = 2;
constexpr int   RUX_SN_MAX              = 0x0000FFFFFFFFFFFF;                                           // Maximum sequnce number
constexpr int   RUX_US_MAX              = 0x0000FFFFFFFFFFFF;                                           // Maximum timestamp(us)
constexpr int   RUX_FRM_MAX             = 92;                                                           // Maximum fragment size
constexpr int   RUX_MSG_MAX             = RUX_FRM_MAX * RUX_MSS;                                        // Maximum signle massage's length
constexpr int   RUX_RTO_MIN             = 50000;
constexpr int   RUX_RTO_MAX             = 6000000;
constexpr int   RUX_RTO_TIMEOUT         = RUX_RTO_MAX * 5;
constexpr int   RUX_FAST_ACK            = 3;
constexpr int   RUX_XMIT_MAX            = 10;


// IPv4����
constexpr char REG_IPV4[] = "^((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4}$";
// IPv6����
constexpr char REG_IPV6[] = "^\\[(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\\]$";


inline int get_ip_type(const std::string& ip) {
    static const std::regex REG_V6(REG_IPV6);
    static const std::regex REG_V4(REG_IPV4);

    if (std::regex_match(ip, REG_V6)) {
        return AF_INET6;
    }
    else if (std::regex_match(ip, REG_V4)) {
        return AF_INET;
    }

    return -1;
}


typedef struct __frame_ {
    uint16_t             len;                // raw data's length
    socklen_t            namelen;            // remote sockaddr's length
    sockaddr_storage     name;               // remote sockaddr
    uint8_t              raw[RUX_MTU + 1];   // raw data

    /* META */
    uint32_t             rid;                // RUX id
    uint8_t              wnd;                // receive window size
    int64_t              time_us;            // receive timestamp(us)
    void*                rux;


    __frame_()
        : len(0)
        , namelen(sizeof(sockaddr))
        , name({ 0,{0} })
        , rid(0)
        , wnd(0)
        , time_us(0) {
        ::memset(raw, 0, RUX_MTU + 1);
    }


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

        return 0;
    }


    int setup() {
        if (rid == 0 || rid > RUX_RID_MAX || wnd > RUX_RWND_MAX) {
            return -1;
        }

        u24_encode(rid, raw);
        u8_encode(wnd, raw + 3);
        return 0;
    }


private:
    __frame_(const __frame_&) = delete;
    __frame_(const __frame_&&) = delete;
    __frame_& operator=(const __frame_&) = delete;
    __frame_& operator=(const __frame_&&) = delete;
} RUX_FRM, *PRUX_FRM;


typedef struct __segment_ {
    uint8_t             cmd;               // command
    uint64_t            sn;                // sequence number
    uint64_t            us;                // timestamp(us)
    uint16_t            len;               // segment's length
    uint8_t             frg;               // fragment
    uint8_t             data[RUX_MSS];     // data: 1424

    /* META */
    uint8_t             fastack;           //
    uint16_t            xmit;              // ���ʹ���
    uint32_t            rto;               //
    uint64_t            resend_us;         
    uint32_t            rid;               // rux id
    int64_t             time_us;           // receive timestamp(us)
    sockaddr_storage*   addr;
    socklen_t           addrlen;


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


    void set_data(const uint8_t* data, uint16_t datalen) {
        ::memcpy(this->data, data, datalen);
        len = datalen;
    }


    int encode(uint8_t* buf, uint16_t buflen) {
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


    // --------------------------------------------
    // segment 解码
    // --------------------------------------------
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


} // namespace net
} // namespace xq


#endif // __XQ_NET_COMMON__
