#include "xq/net.hpp"

const char* IP = "0.0.0.0";
const char* PORT = "6688";

class EchoEvent : public virtual xq::net::IEvent {
public:
	typedef std::shared_ptr<EchoEvent> Ptr;

	// Í¨¹ý IEvent ¼Ì³Ð
	virtual int on_message(xq::net::KcpConn* conn, const char* data, int data_len) override {
		printf("%s\n", std::string(data, data_len).c_str());
		return conn->send(data, data_len);
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
	auto listener = xq::net::KcpListener::create();
	listener->run(xq::net::IEvent::Ptr(new EchoEvent), IP, PORT, 2, 100);
	xq::net::release();
	printf("DONE...%lld\n", xq::tools::get_time_ms());
}