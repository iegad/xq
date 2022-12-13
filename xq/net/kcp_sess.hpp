#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "third/blockingconcurrentqueue.h"
#include "spdlog/spdlog.h"
#include "net/net.hpp"

namespace xq {
namespace net {

struct KcpSeg {
	typedef std::shared_ptr<KcpSeg> Ptr;

	static Ptr create(uint8_t *data, size_t len, const sockaddr *addr, socklen_t addrlen) {
		return Ptr(new KcpSeg(data, len, addr, addrlen));
	}

	uint8_t* data;
	size_t len;
	sockaddr addr;
	socklen_t addrlen;

private:
	KcpSeg(uint8_t* data, size_t len, const sockaddr* dst, socklen_t addrlen)
		: data(data)
		, len(len)
		, addrlen(addrlen) {
		::memcpy(&addr, dst, addrlen);
	}
}; // struct KcpSeg;

class KcpListener;

class KcpSess {
public:
	typedef std::shared_ptr<KcpSess> Ptr;
	typedef int (*Output)(const char*, int, IKCPCB*, void*);

	static Ptr create(moodycamel::BlockingConcurrentQueue<KcpSeg::Ptr>& que, uint32_t conv, int32_t timeout, Output func) {
		return Ptr(new KcpSess(que, conv, KCP_DEFAULT_TIMEOUT, func));
	}

	~KcpSess() {
		release();
	}

	std::pair<const sockaddr*, socklen_t> addr() const {
		return std::make_pair(&addr_, addrlen_);
	}

	moodycamel::BlockingConcurrentQueue<KcpSeg::Ptr>& que() {
		return que_;
	}

	uint32_t conv() {
		std::lock_guard<std::mutex> lk(mtx_);
		return kcp_->conv;
	}

	bool change(SOCKET sockfd, sockaddr* addr, socklen_t addrlen) {
		std::lock_guard<std::mutex> lk(mtx_);

		if (sockfd_ != sockfd)
			sockfd_ = sockfd;

		if (addrlen_ == addrlen && !::memcmp(&addr_, addr, addrlen))
			return false;

		bool res = addr_.sa_family != 0;
		ikcp_reset(kcp_);
		sockfd_ = sockfd;
		::memcpy(&addr_, addr, addrlen);
		addrlen_ = addrlen;
		time_ = xq::tools::now_milli();
		last_active_ = time_ / 1000;
		return res;
	}

	int input(const uint8_t *raw, size_t raw_len) {
		std::lock_guard<std::mutex> lk(mtx_);
		return ::ikcp_input(kcp_, raw, raw_len);
	}

	int recv(uint8_t* buf, size_t buflen) {
		std::lock_guard<std::mutex> lk(mtx_);
		last_active_ = xq::tools::now();
		return ::ikcp_recv(kcp_, buf, buflen);
	}

	int update(int64_t now_ms, int64_t timeout = 0) {
		std::lock_guard<std::mutex> lk(mtx_);
		uint32_t now = (uint32_t)(now_ms - time_);
		::ikcp_update(kcp_, now);
		if (timeout > 0 && now_ms / 1000 - last_active_ > timeout)
			return -1;

		return 0;
	}

	void flush() {
		std::lock_guard<std::mutex> lk(mtx_);
		::ikcp_flush(kcp_);
	}

	int send(const uint8_t* data, size_t data_len) {
		std::lock_guard<std::mutex> lk(mtx_);
		int n = ::ikcp_send(kcp_, data, data_len);
		if (!n) ::ikcp_flush(kcp_);
		return n;
	}

	bool active() {
		std::lock_guard<std::mutex> lk(mtx_);
		return kcp_ != nullptr;
	}

private:
	KcpSess(moodycamel::BlockingConcurrentQueue<KcpSeg::Ptr>& que, uint32_t conv, int32_t timeout, Output func)
		: kcp_(nullptr)
		, sockfd_(INVALID_SOCKET)
		, addr_({0, 0})
		, addrlen_(sizeof(sockaddr))
		, time_(xq::tools::now_milli())
		, last_active_(time_ / 1000)
		, que_(que) {
		kcp_ = ::ikcp_create(conv, this);
		assert(kcp_);

		kcp_->output = func;
		assert(!::ikcp_nodelay(kcp_, 1, 10, 1, 1));
		assert(!::ikcp_wndsize(kcp_, 512, 512));

		kcp_->updated = 1;
	}

	void release() {
		std::lock_guard<std::mutex> lk(mtx_);

		if (kcp_) {
			::ikcp_release(kcp_);
			kcp_ = nullptr;
		}
		time_ = last_active_ = 0;
	}

	IKCPCB *kcp_;

	SOCKET sockfd_;
	sockaddr addr_;
	socklen_t addrlen_;

	int64_t time_;
	int64_t last_active_;
	std::mutex mtx_;
	moodycamel::BlockingConcurrentQueue<KcpSeg::Ptr>& que_;
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__