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


    void __inline__ cong_avoid(int *cwnd) {
        int wnd = *cwnd;
        if (wnd < ssthresh_) {
            wnd <<= 1;
        }
        else {
            wnd += 1;
        }

        if (wnd > CCC_CWND_MAX) {
            wnd = CCC_CWND_MAX;
        }

        *cwnd = wnd;
    }


    void __inline__ loss(int *cwnd) {

    }


    void __inline__ fast(int* cwnd) {

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
