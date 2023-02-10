#include "xq/net/net.hpp"
#include "xq/tools/tools.hpp"
#include "xq/net/kcp_conn.hpp"

xq::net::KcpConn::Ptr conn;

//const std::string host = "1.15.81.179:6688";
const std::string host = "192.168.0.101:6688";

void worker() {
	std::this_thread::sleep_for(std::chrono::seconds(1));

	char buf[100] = { 0 };
	for (int i = 0; i < 5000; i++) {
		sprintf(buf, "Hello world: %d", i);
		int n = conn->send(host, (uint8_t *)buf, strlen(buf));
		if (n < 0) {
			std::printf("conn->send failed");
		}
	}
}

int
main(int argc, char** argv) {
#ifdef _WIN32
	WSADATA wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202)
		exit(EXIT_FAILURE);
#endif // _WIN32
	std::vector<std::string> hosts;
	hosts.emplace_back(host);
	conn = xq::net::KcpConn::create(1, ":0", hosts);
	std::thread(worker).detach();
	conn->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32

	exit(EXIT_SUCCESS);
}