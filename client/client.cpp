#include "xq/net.hpp"

static int NTIME = 100;

void
update_worker(xq::net::KcpConn::Ptr conn) {
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (conn->update(xq::tools::get_time_ms())) {
			break;
		}
	}
}

void
send_worker(xq::net::KcpConn::Ptr conn) {
	char data[1024 * 8];
	int i;
	for (i = 0; i < NTIME; i++) {
		::sprintf(data, "Hello world: %d !!!", i);
		if (conn->send(data, ::strlen(data))) {
			break;
		}
	}
	printf("SEND_WORK HAS DONE... %d\n", i);
}

int
main(int argc, char** argv) {
	assert(!xq::net::init());

	xq::net::KcpConn::Ptr conn = xq::net::KcpConn::connect("127.0.0.1", "6688", 1);

	std::thread(std::bind(update_worker, conn)).detach();
	std::thread(std::bind(send_worker, conn)).detach();
	
	char rbuf[xq::net::DEFAULT_KCP_MTU] = { 0 };
	char data[1024 * 4] = { 0 };
	int i = 0;
	for (;;) {
		if (i == NTIME) {
			break;
		}

		int n = ::recvfrom(conn->sockfd(), rbuf, sizeof(rbuf), 0, nullptr, nullptr);
		if (n <= 0) {
			printf("recvfrom failed: %d\n", xq::net::error());
			break;
		}
		printf("--------: %d", i);
		if (n > 24) {
			n = conn->recv(rbuf, n, data, sizeof(data));
			if (n > 0) {
				printf(" => %s\n", std::string(data, n).c_str());
				i++;
			}
		}
	}

	printf("CLIENT HAS FINISHEDDDDDDDDDDDDDD\n");
	xq::net::release();
}