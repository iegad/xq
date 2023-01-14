#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"

namespace xq {
namespace net {

class KcpSess: public Kcp {
public:
	typedef std::shared_ptr<KcpSess>  Ptr;

	static Ptr create(uint32_t conv) {
		return Ptr(new KcpSess(conv));
	}

	std::pair<sockaddr*, socklen_t> addr() {
		return std::make_pair(&addr_, addrlen_);
	}

	void set_ufd(SOCKET ufd) {
		if (ufd_ != ufd) {
			ufd_ = ufd;
		}
	}

	uint32_t ufd() {
		return ufd_;
	}

	int update(int64_t now_ms) {
		if (ufd_ == INVALID_SOCKET || !state() || now_ms - last_ms_ > KCP_DEFAULT_TIMEOUT) {
			return -1;
		}

		uint32_t ts = now_ms - time_ms_;
		Kcp::update(ts);

		return 0;
	}

	bool check_new(const sockaddr* addr, socklen_t addrlen) {
		bool res = addrlen != addrlen_;
		if (res || ::memcmp(addr, &addr_, addrlen)) {
			if (res) {
				addrlen_ = addrlen;
			}
			
			::memcpy(&addr_, addr, addrlen_);
			reset();
			return true;
		}

		return false;
	}

private:
	KcpSess(uint32_t conv)
		: Kcp(conv, this)
		, ufd_(INVALID_SOCKET)
		, time_ms_(xq::tools::now_milli())
		, last_ms_(time_ms_)
		, addr_({{0},0})
		, addrlen_(sizeof(addr_)) {
	}

	SOCKET ufd_;
	int64_t time_ms_;
	int64_t last_ms_;
	sockaddr addr_;
	socklen_t addrlen_;

	KcpSess(const KcpSess&) = delete;
	KcpSess& operator=(const KcpSess&) = delete;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
