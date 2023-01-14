#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"
#include "third/blockingconcurrentqueue.h"
#include <functional>
#include <unordered_set>

namespace xq {
namespace net {

struct KcpSeg {
	static xq::tools::ObjectPool<KcpSeg>* pool() {
		return xq::tools::ObjectPool<KcpSeg>::Instance();
	}

	uint8_t* data;
	size_t len;
	KcpSess* sess;

	KcpSeg()
		: data(new uint8_t[KCP_MAX_DATA_SIZE])
		, len(KCP_MAX_DATA_SIZE)
		, sess(nullptr) {
		assert(data);
	}

	~KcpSeg() {
		delete[] data;
	}
}; // struct KcpSeg;

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
			wthread_pool_.emplace_back(std::thread(std::bind(&KcpListener::_work_thread, this)));
		}

		update_thread_ = std::thread(std::bind(&KcpListener::_update, this));
		io_thread_ = std::thread(std::bind(&KcpListener::_io, this));
		
		io_thread_.join();
		update_thread_.join();

		for (auto &t : wthread_pool_) {
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

	void _unactive_sessions(const std::vector<uint32_t> &convs) {
		as_mtx_.lock();
		for (auto conv : convs) {
			active_sessions_.erase(conv);
		}
		as_mtx_.unlock();
	}

	std::vector<uint32_t> _get_active_sessions() {
		as_mtx_.lock();
		std::vector<uint32_t> rzt(active_sessions_.begin(), active_sessions_.end());
		as_mtx_.unlock();
		return rzt;
	}

	void _io() {
		// Step 1: 创建 udp 监听套接字
		ufd_ = udp_socket(host_.c_str(), nullptr);
		assert(ufd_ != INVALID_SOCKET && "ufd create failed");

#ifndef WIN32
		constexpr uint32_t RCVBUF_SIZE = 1024 * 1024 * 32;
		socklen_t opt_len = sizeof(RCVBUF_SIZE);
		assert(::setsockopt(ufd_, SOL_SOCKET, SO_RCVBUF, &RCVBUF_SIZE, opt_len) == 0 && "set udp recv buffer size failed");
#endif // WIN32

		sockaddr addr;
		socklen_t addrlen = sizeof(addr);
		char raw[KCP_MTU];
		size_t rawlen = -1;
		char* rbuf = new char[KCP_MAX_DATA_SIZE];
		int nrecv = 0;

		uint32_t conv;
		KcpSess::Ptr sess;

		while (state_ == State::Running) {
			// Step 2: 获取数据
			rawlen = ::recvfrom(ufd_, raw, KCP_MTU, 0, &addr, &addrlen);
			if (rawlen < 0) {
				printf("recvfrom error: %d\n", error());
				continue;
			}

			// Step 3: 获取对应的KcpSession
			conv = Kcp::get_conv(raw);
			sess = sessions_[conv];
			if (sess->check_new(&addr, addrlen)) {
				sess->set_ufd(ufd_);
				_active_sess(conv);
			}

			// Step 4: 获取KCP消息包
			if (sess->input(raw, rawlen) < 0) {
				_unactive_sess(conv);
				continue;
			}

			do {
				// Step 5: 获取消息包
				nrecv = sess->recv(rbuf, KCP_MAX_DATA_SIZE);
				if (nrecv < 0) {
					break;
				}

				KcpSeg* seg = KcpSeg::pool()->get();
				seg->sess = sess.get();
				::memcpy(seg->data, rbuf, nrecv);
				seg->len = nrecv;

				// Step 6: 投递消息
				if (!que_.enqueue(seg)) {
					printf("que_.enqueue failed\n");
					break;
				}
			} while (sess->rque_count());
		}

		delete[] rbuf;
	}

	void _update() {
		constexpr std::chrono::milliseconds INTVAL = std::chrono::milliseconds(10);

		int64_t now_ms = xq::tools::now_milli();
		std::vector<uint32_t> unactived;

		while (state_ == State::Running) {
			now_ms = xq::tools::now_milli();
			std::this_thread::sleep_for(INTVAL);
			std::vector<uint32_t> aq = _get_active_sessions();

			for (auto conv : aq) {
				auto itr = sessions_.find(conv);
				assert(itr != sessions_.end());
				KcpSess::Ptr s = itr->second;
				if (s->update(now_ms) < 0) {
					unactived.push_back(conv);
				}
			}

			_unactive_sessions(unactived);
			unactived.clear();
		}
	}

	void _work_thread() {
		while (state_ == State::Running) {
			KcpSeg* seg;
			que_.wait_dequeue(seg);

			KcpSess* s = seg->sess;

			if (s->send((char *)seg->data, seg->len) < 0) {
				_unactive_sess(s->conv());
			}

			KcpSeg::pool()->put(seg);
		}
	}

	const uint32_t max_conn_;

	State state_;

	SOCKET ufd_;
	std::string host_;

	std::mutex as_mtx_;
	std::unordered_set<uint32_t> active_sessions_;

	std::vector<std::thread> wthread_pool_;
	std::thread io_thread_;
	std::thread update_thread_;

	moodycamel::BlockingConcurrentQueue<KcpSeg*> que_;

	std::unordered_map<uint32_t, KcpSess::Ptr> &sessions_;
}; // class KcpListener;

} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__
