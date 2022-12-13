#ifndef __KCP_LISTENER__
#define __KCP_LISTENER__

#include "net/net.hpp"
#include "net/kcp_sess.hpp"
#include "third/blockingconcurrentqueue.h"
#include "spdlog/sinks/basic_file_sink.h"

namespace xq {
namespace net {

class KcpListener final {
public:
	typedef std::unique_ptr<KcpListener> ptr;

	enum class State {
		Stopped = 0,
		Stopping,
		Runing,
	};

	static ptr create(IListenerEvent::Ptr event, const std::string &host, uint32_t timeout = 0, uint32_t nthread = 0) {
		if (!timeout)
			timeout = KCP_DEFAULT_TIMEOUT;

		if (!nthread)
			nthread = std::thread::hardware_concurrency();

		return ptr(new KcpListener(event, host, timeout, nthread));
	}

	~KcpListener() {
		stop();
	}

	void run();
	void stop();

private:
	explicit KcpListener(IListenerEvent::Ptr event, const std::string &host, uint32_t timeout, uint32_t nthread)
		: host_(host)
		, state_(State::Stopped)
		, timeout_(timeout)
		, nthread_(nthread)
		, event_(event) {
		assert(timeout_ > 0 && "timeout is invalid");
		assert(nthread_ > 0 && "nthread is invalid");

		log_ = spdlog::basic_logger_mt("log", "logs/server.log");
	}

	static int _udp_output(const char* data, int datalen, IKCPCB* kcp, void* user);

	void _update_thr();
	void _recv_thr(SOCKET sockfd);
	void _send_thr(SOCKET sockfd);

	std::string host_;
	State state_;
	uint32_t timeout_;
	uint32_t nthread_;

	std::thread update_thr_;
	std::vector<SOCKET> ufds_;
	std::vector<std::thread> recv_pool_;
	std::vector<std::thread> send_pool_;
	xq::tools::Map<uint32_t, KcpSess::Ptr> sess_map_;

	moodycamel::BlockingConcurrentQueue<KcpSeg::Ptr> que_;

	IListenerEvent::Ptr event_;

	std::shared_ptr<spdlog::logger> log_;

	KcpListener(const KcpListener&) = delete;
	KcpListener& operator=(const KcpListener&) = delete;
}; // class KcpListener;


} // namespace net;
} // namespace xq;

#endif // __KCP_LISTENER__