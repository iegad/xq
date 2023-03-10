#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"
#include "xq/net/kcp_conn.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <csignal>

#ifndef WIN32
#include <jemalloc/jemalloc.h>
#endif

const std::string host = "1.15.81.179:6688";
//const std::string host = "192.168.0.101:6688";
constexpr int NTIMES = 200000;

class EchoEvent {
public:
	typedef xq::net::KcpConn<EchoEvent> KcpConn;
	typedef KcpConn::Host KcpHost;

	int on_message(KcpHost* host, const uint8_t* data, size_t datalen) {
		std::string tmp = std::string((const char*)data, datalen);
		spdlog::info(tmp);
		logger_->info(tmp);
		return 0;
	}

	void on_error(xq::net::ErrType err_type, int err, void* arg) {

	}


	int on_recv(uint8_t* raw, size_t rawlen, const sockaddr* addr, socklen_t addrlen) {
		return 0;
	}


	void on_send(const uint8_t* raw, size_t rawlen, const sockaddr* addr, socklen_t addrlen) {

	}

	EchoEvent() 
		: logger_(spdlog::rotating_logger_st("logger", SPDLOG_FILENAME_T("log.txt"), 1024 * 1024 * 1024, 0))
	{}

	~EchoEvent() {
		logger_->flush();
	}

private:
	std::shared_ptr<spdlog::logger> logger_;
};

EchoEvent::KcpConn::Ptr conn;

void worker() {
	std::this_thread::sleep_for(std::chrono::seconds(1));

	char* buf = new char[xq::net::KCP_MAX_DATA_SIZE];
	::memset(buf, 0, xq::net::KCP_MAX_DATA_SIZE);

	int i = 0;
	for (i = 0; i < NTIMES;) {
		sprintf(buf, "---- %d ---- 12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", i);
		
		int n = conn->send(host, (uint8_t *)buf, strlen(buf));
		if (n < 0) {
			std::printf("conn->send failed: %d\n", n);
			exit(1);
		}
		else if (n == 1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		else {
			i++;
		}
		std::this_thread::yield();
	}

	delete[] buf;
}

#include "xq/third/AES.h"

void signal_handler(int signal) {
	if (signal == SIGINT && conn) {
		conn->stop();
	}
}

int
main(int argc, char** argv) {
	if (argc < 2) {
		std::printf("ÇëÊäÈëconv\b");
		exit(EXIT_FAILURE);
	}

	uint32_t conv = std::stoi(argv[1]);

#ifdef _WIN32
	WSADATA wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202)
		exit(EXIT_FAILURE);
#endif // _WIN32
	std::vector<std::string> hosts{host};
	conn = EchoEvent::KcpConn::create(conv, ":0", hosts);

	std::signal(SIGINT, signal_handler);

	std::thread(worker).detach();
	conn->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(EXIT_SUCCESS);
}