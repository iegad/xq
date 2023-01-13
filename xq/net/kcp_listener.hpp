#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"
#include "third/blockingconcurrentqueue.h"
#include <functional>
#include <unordered_set>

namespace xq {
namespace net {

class KcpListener {
public:
	typedef std::shared_ptr<KcpListener> Ptr;

	enum class State {
		Stopped,
		Stopping,
		Running
	};

	static Ptr create(const std::string& host, uint32_t max_conn) {
		return Ptr(new KcpListener(host, max_conn));
	}

	~KcpListener() {
		close(ufd_);
	}

	void run() {
		state_ = State::Running;

		for (size_t i = 0, n = std::thread::hardware_concurrency(); i < n; i++) {
			thread_pool_.emplace_back(std::thread(std::bind(&KcpListener::_thread, this)));
		}

		update_thread_ = std::thread(std::bind(&KcpListener::_update, this));
		io_thread_ = std::thread(std::bind(&KcpListener::_io, this));
		
		io_thread_.join();
		update_thread_.join();

		for (auto &t : thread_pool_) {
			t.join();
		}

		KcpSeg* item[10];
		while (que_.try_dequeue_bulk(item, 10));

		state_ = State::Stopped;
	}

	void stop() {
		state_ = State::Stopping;
	}

private:
	KcpListener(const std::string& host, uint32_t max_conn)
		: max_conn_(max_conn)
		, state_(State::Stopped)
		, ufd_(INVALID_SOCKET)
		, host_(host)
		, sessions_(Kcp::sessions()) {

		assert(max_conn_ > 0 && "max_conn is invalid");

		sessions_.clear();
		for (uint32_t conv = 1; conv <= max_conn; conv++) {
			KcpSess::Ptr s = KcpSess::create(conv);
			s->nodelay(1, 20, 2, 1);
			s->set_output(KcpListener::output);
			sessions_[conv] = s;
		}
	}

	static int output(const char* raw, int len, IKCPCB* kcp, void* user) {
		KcpSess* s = (KcpSess*)user;
		std::pair<sockaddr*, socklen_t> addr = s->addr();
		if (::sendto(s->ufd(), raw, len, 0, addr.first, addr.second) < 0) {
			printf("sendto error: %d\n", error());
		}

		return 0;
	}

	void _active_sess(uint32_t conv) {
		as_mtx_.lock();
		active_sessions_.insert(conv);
		as_mtx_.unlock();
	}

	void _unactive_sess(uint32_t conv) {
		as_mtx_.lock();
		active_sessions_.erase(conv);
		as_mtx_.unlock();
	}

	std::vector<uint32_t> _get_active_sessions() {
		as_mtx_.lock();
		std::vector<uint32_t> rzt(active_sessions_.begin(), active_sessions_.end());
		as_mtx_.unlock();
		return rzt;
	}

	void _io() {
		ufd_ = udp_socket(host_.c_str(), nullptr);
		assert(ufd_ != INVALID_SOCKET && "ufd create failed");

		sockaddr addr;
		socklen_t addrlen = sizeof(addr);
		char raw[KCP_MTU];
		size_t rawlen = -1;

		uint32_t conv;
		KcpSess::Ptr sess;

		while (state_ == State::Running) {
			rawlen = ::recvfrom(ufd_, raw, KCP_MTU, 0, &addr, &addrlen);
			if (rawlen < 0) {
				printf("recvfrom error: %d\n", error());
				continue;
			}

			conv = Kcp::get_conv(raw);
			sess = sessions_[conv];

			if (sess->input(raw, rawlen) < 0) {
				_unactive_sess(conv);
				continue;
			}

			sess->flush();

			KcpSeg* seg = KcpSeg::Pool::Instance()->get();

			do {
				if (sess->recv(raw, KCP_MTU) < 0) {
					break;
				}

				if (!que_.enqueue(seg)) {
					printf("que_.enqueue failed\n");
					break;
				}
			} while (sess->peeksize());
		}
	}

	void _update() {
		while (state_ == State::Running) {
			
		}
	}

	void _thread() {
		while (state_ == State::Running) {

		}
	}

	const uint32_t max_conn_;

	State state_;

	SOCKET ufd_;
	std::string host_;

	std::mutex as_mtx_;
	std::unordered_set<uint32_t> active_sessions_;

	std::vector<std::thread> thread_pool_;
	std::thread io_thread_;
	std::thread update_thread_;

	moodycamel::BlockingConcurrentQueue<KcpSeg*> que_;

	std::unordered_map<uint32_t, KcpSess::Ptr> &sessions_;
}; // class KcpListener;

} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__
