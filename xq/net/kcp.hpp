#ifndef __NET_KCP__
#define __NET_KCP__

#include "third/ikcp.h"
#include <memory>
#include <mutex>

namespace xq {
namespace net {

class KcpSess;

class Kcp {
public:
	typedef std::shared_ptr<Kcp> Ptr;
	typedef std::shared_ptr<KcpSess> KSPtr;

	static std::unordered_map<uint32_t, KSPtr>& sessions() {
		static std::unordered_map<uint32_t, KSPtr> m_;
		return m_;
	}

	static Ptr create(uint32_t conv, void *user) {
		return Ptr(new Kcp(conv, user));
	}

	static uint32_t get_conv(const void *raw) {
		return ::ikcp_getconv(raw);
	}

	~Kcp() {
		mtx_.lock();
		if (kcp_) {
			::ikcp_release(kcp_);
		}
		mtx_.unlock();
	}

	void set_output(int (*output)(const char* buf, int len, ikcpcb* kcp, void* user)) {
		mtx_.lock();
		kcp_->output = output;
		mtx_.unlock();
	}

	int recv(char* buf, int len) {
		mtx_.lock();
		int rzt = ::ikcp_recv(kcp_, buf, len);
		mtx_.unlock();
		return rzt;
	}

	int send(const char* buf, int len) {
		mtx_.lock();
		int rzt = ::ikcp_send(kcp_, buf, len);
		mtx_.unlock();
		return rzt;
	}

	void update(uint32_t current) {
		mtx_.lock();
		::ikcp_update(kcp_, current);
		mtx_.unlock();
	}

	int input(const char* data, long size) {
		mtx_.lock();
		int rzt = ::ikcp_input(kcp_, data, size);
		mtx_.unlock();
		return rzt;
	}

	void flush() {
		mtx_.lock();
		::ikcp_flush(kcp_);
		mtx_.unlock();
	}

	int peeksize() {
		mtx_.lock();
		int rzt = ::ikcp_peeksize(kcp_);
		mtx_.unlock();
		return rzt;
	}

	int nodelay(int nodelay, int interval, int resend, int nc) {
		mtx_.lock();
		int rzt = ::ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
		mtx_.unlock();
		return rzt;
	}

	uint32_t conv() {
		return kcp_->conv;
	}

protected:
	Kcp(uint32_t conv, void* user)
		: kcp_(::ikcp_create(conv, user)) {
		::ikcp_setmtu(kcp_, 512);
	}

private:
	IKCPCB* kcp_;
	std::mutex mtx_;

	Kcp(const Kcp&);
	Kcp& operator=(const Kcp&);
}; // class Kcp

} // namespace net
} // namespace xq


#endif // !__NET_KCP__
