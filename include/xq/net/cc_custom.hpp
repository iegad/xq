#ifndef __XQ_NET_VEGAS__
#define __XQ_NET_VEGAS__


#include "xq/net/net.hpp"


namespace xq {
namespace net {


constexpr int CCC_SSTHRESH_INIT = 16;
constexpr int CCC_CWND_MAX = 128;
constexpr int CCC_CWND_MIN = 2;
constexpr int CCC_ALPHA = 2;
constexpr int CCC_BETA = 4;
constexpr int CCC_GAMMA = 1;
constexpr int CCC_COLL_TIME = 5000;
constexpr int CCC_CNT_MAX = 10;


class CCCustom {
public:
    CCCustom()
        : ssthresh_(CCC_SSTHRESH_INIT)
        , base_rtt_(0)
        , min_rtt_(0)
        , cnt_rtt_(0)
    {}


    __inline__ void update_ack(int64_t rtt, int *cwnd) {
        rtt += 1;

        if (cnt_rtt_ > CCC_CNT_MAX) {
            cnt_rtt_ = 0;
            min_rtt_ = 0;
        }

        if (base_rtt_ == 0 || base_rtt_ > rtt) {
            base_rtt_ = rtt;
        }

        if (min_rtt_ == 0 || rtt < min_rtt_) {
            min_rtt_ = rtt;
        }

        int diff = *cwnd * (rtt - base_rtt_) / base_rtt_;
        if (diff > CCC_BETA) {
            if (*cwnd < ssthresh_) {
                *cwnd -= 1;
            }
            else {
                *cwnd >>= 1;
            }
        }
        else if (diff < CCC_ALPHA) {
            if (*cwnd < ssthresh_) {
                *cwnd <<= 1;
            }
            else {
                *cwnd += 1;
            }
        }

        if (*cwnd < CCC_CWND_MIN) {
            *cwnd = CCC_CWND_MIN;
        }
        else if (*cwnd > CCC_CWND_MAX) {
            *cwnd = CCC_CWND_MAX;
        }

        std::printf("CCC diff: %d cwnd: %d\n", diff, *cwnd);
        cnt_rtt_++;
    }


    __inline__ void loss(int *cwnd) {

    }


    __inline__ void fastack(int* cwnd) {

    }


private:
    int ssthresh_;
    int base_rtt_;
    int min_rtt_;
    int cnt_rtt_;;


    CCCustom(const CCCustom&) = delete;
    CCCustom(const CCCustom&&) = delete;
    CCCustom& operator=(const CCCustom&) = delete;
    CCCustom& operator=(const CCCustom&&) = delete;
}; // class Vegas;


} // namespace net
} // namespace xq;

#endif // !__XQ_NET_VEGAS__
