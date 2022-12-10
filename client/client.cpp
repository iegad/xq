#include "xq/net.hpp"
#include "xq/tools.hpp"

static int NTIME = 10000;

void
update_worker(xq::net::KcpConn::Ptr conn) {
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (conn->update(xq::tools::now_milli()) < 0) {
			break;
		}
	}

	printf("UPDATE WORK HAS DONE...\n");
}

void
send_worker(xq::net::KcpConn::Ptr conn) {
	char *data = new char[xq::net::MAX_DATA_SIZE];
	int i = 0;
	while (1) {
		if (NTIME > 0 && NTIME == i)
			break;

		::sprintf(data, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA: %d!!!", i);
		if (conn->send(data, strlen(data)))
			break;

		i++;
	}

	delete[] data;
	printf("SEND_WORK HAS DONE... %d\n", i);
}

class EchoEvent : public virtual xq::net::IEvent {
public:
	typedef std::shared_ptr<EchoEvent> Ptr;

	virtual int on_message(xq::net::KcpConn* conn, const char* data, size_t data_len) override {
		static int ntime = 0;
		printf("%s\n", std::string(data, data_len).c_str());
		ntime++;
		return NTIME > 0 && ntime == NTIME ? -1 : 0;
	}

	virtual int on_connected(xq::net::KcpConn* conn) override {
		printf("%d has connected\n", conn->conv());
		return 0;
	}

	virtual void on_disconnected(xq::net::KcpConn* conn) override {
		printf("%d has disconnected\n", conn->conv());
	}

	virtual void on_send(xq::net::KcpConn* conn, const char* raw, size_t raw_len) override {
		//const char* p = raw;
		//size_t nleft = raw_len;

		//uint32_t conv, cmd, frg, wnd, ts, sn, una, len;
		//while (nleft > 0) {
		//	conv = *(uint32_t*)p;
		//	p += 4; nleft -= 4;

		//	cmd = *p; p += 1; nleft -= 1;
		//	frg = *p; p += 1; nleft -= 1;
		//	wnd = *(uint16_t*)p; p += 2; nleft -= 2;
		//	ts = *(uint32_t*)p; p += 4; nleft -= 4;
		//	sn = *(uint32_t*)p; p += 4; nleft -= 4;
		//	una = *(uint32_t*)p; p += 4; nleft -= 4;
		//	len = *(uint32_t*)p; p += 4; nleft -= 4;

		//	printf("%lld => {conv[%u]; cmd[%u]; frg[%u]; wnd[%u]; ts[%u]; sn[%u]; una[%u]; len[%u]; data: %s}\n", ::time(nullptr), conv, cmd, frg, wnd, ts, sn, una, len, std::string(p, len).c_str());
		//	p += len;
		//	nleft -= len;
		//}
	}
};

int
main(int argc, char** argv) {
#ifdef _WIN32
	WSADATA wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202)
		exit(EXIT_FAILURE);
#endif // _WIN32

	xq::net::KcpConn::Ptr conn = xq::net::KcpConn::connect(xq::net::IEvent::Ptr(new EchoEvent), nullptr, argc > 1 ? argv[1] : "127.0.0.1:6688", argc > 2 ? std::stoul(argv[2]) : 0x01);

	std::thread(std::bind(update_worker, conn)).detach();
	std::thread(std::bind(send_worker, conn)).detach();

	char data[1024 * 8];
	uint64_t beg = xq::tools::now_milli();
	for (;;) {
		if (conn->recv() < 0)
			break;
	}

	printf("CLIENT HAS FINISHEDDDDDDDDDDDDDD => %llu\n", xq::tools::now_milli() - beg);

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32

	exit(EXIT_SUCCESS);
}