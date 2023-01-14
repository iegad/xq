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
		if (rzt == 0) {
			::ikcp_flush(kcp_);
		}
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
		if (rzt == 0) {
			ikcp_flush(kcp_);
		}
		mtx_.unlock();
		return rzt;
	}

	bool state() {
		mtx_.lock();
		bool rzt = kcp_->state == 0;
		mtx_.unlock();
		return rzt;
	}

	int nodelay(int nodelay, int interval, int resend, int nc) {
		mtx_.lock();
		int rzt = ::ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
		mtx_.unlock();
		return rzt;
	}

	size_t rque_count() {
		return kcp_->nrcv_que;
	}

	uint32_t conv() {
		return kcp_->conv;
	}

	void reset() {
		mtx_.lock();
		IKCPSEG* seg;
		while (!IQUEUE_IS_EMPTY(&kcp_->snd_buf)) {
			seg = IQUEUE_ENTRY(kcp_->snd_buf.next, IKCPSEG, node);
			IQUEUE_DEL(&seg->node);
			::free(seg);
		}
		while (!IQUEUE_IS_EMPTY(&kcp_->rcv_buf)) {
			seg = IQUEUE_ENTRY(kcp_->rcv_buf.next, IKCPSEG, node);
			IQUEUE_DEL(&seg->node);
			::free(seg);
		}
		while (!IQUEUE_IS_EMPTY(&kcp_->snd_queue)) {
			seg = IQUEUE_ENTRY(kcp_->snd_queue.next, IKCPSEG, node);
			IQUEUE_DEL(&seg->node);
			::free(seg);
		}
		while (!IQUEUE_IS_EMPTY(&kcp_->rcv_queue)) {
			seg = IQUEUE_ENTRY(kcp_->rcv_queue.next, IKCPSEG, node);
			IQUEUE_DEL(&seg->node);
			::free(seg);
		}

		kcp_->snd_una = 0;
		kcp_->snd_nxt = 0;
		kcp_->rcv_nxt = 0;
		kcp_->ts_recent = 0;
		kcp_->ts_lastack = 0;
		kcp_->ts_probe = 0;
		kcp_->probe_wait = 0;
		kcp_->cwnd = 0;
		kcp_->incr = 0;
		kcp_->probe = 0;
		kcp_->stream = 0;

		kcp_->nrcv_buf = 0;
		kcp_->nsnd_buf = 0;
		kcp_->nrcv_que = 0;
		kcp_->nsnd_que = 0;
		kcp_->state = 0;
		kcp_->ackblock = 0;
		kcp_->ackcount = 0;
		kcp_->rx_srtt = 0;
		kcp_->rx_rttval = 0;
		kcp_->current = 0;
		kcp_->nodelay = 0;
		kcp_->updated = 0;
		kcp_->logmask = 0;
		kcp_->fastresend = 0;
		kcp_->nocwnd = 0;
		kcp_->xmit = 0;
		mtx_.unlock();
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
