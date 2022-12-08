#include "xq/net.hpp"
#include "xq/tools.hpp"

static int NTIME = 1000;

void
update_worker(xq::net::KcpConn::Ptr conn) {
	for (;;) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (conn->update(xq::tools::get_time_ms()) < 0) {
			break;
		}
	}

	printf("UPDATE WORK HAS DONE...\n");
}

void
send_worker(xq::net::KcpConn::Ptr conn) {
	char data[1024 * 8];
	int i;
	for (i = 0; i < NTIME; i++) {
		::sprintf(data, "%06d", i);
		if (conn->send(data, ::strlen(data))) {
			break;
		}
	}
	printf("SEND_WORK HAS DONE... %d\n", i);
}

class EchoEvent : public virtual xq::net::IEvent {
public:
	typedef std::shared_ptr<EchoEvent> Ptr;

	virtual int on_message(xq::net::KcpConn* conn, const char* data, int data_len) override {
		static int ntime = 0;
		printf("%s\n", std::string(data, data_len).c_str());
		ntime++;
		return ntime == NTIME ? -1 : 0;
	}

	virtual int on_connected(xq::net::KcpConn* conn) {
		printf("%d has connected\n", conn->conv());
		return 0;
	}

	virtual void on_disconnected(xq::net::KcpConn* conn) {
		printf("%d has disconnected\n", conn->conv());
	}
};

int
main(int argc, char** argv) {
	assert(!xq::net::init());

	xq::net::KcpConn::Ptr conn = xq::net::KcpConn::connect(xq::net::IEvent::Ptr(new EchoEvent), nullptr, argc > 1 ? argv[1] : "127.0.0.1:6688", 288);

	std::thread(std::bind(update_worker, conn)).detach();
	std::thread(std::bind(send_worker, conn)).detach();

	for (;;) {
		if (conn->recv() < 0)
			break;
	}

	printf("CLIENT HAS FINISHEDDDDDDDDDDDDDD\n");
	xq::net::release();
}