#include "xq/net.hpp"

const char* IP = "0.0.0.0";
const char* PORT = "6688";

class EchoEvent : public virtual xq::net::IEvent {
public:
	typedef std::shared_ptr<EchoEvent> Ptr;

	// ͨ�� IEvent �̳�
	virtual int on_message(xq::net::KcpConn* conn, const char* data, int data_len) override {
		printf("%s\n", std::string(data, data_len).c_str());
		return conn->send(data, data_len);
	}
};

int
main(int argc, char** argv) {
	assert(!xq::net::init());
	auto listener = xq::net::KcpListener::create(xq::net::IEvent::Ptr(new EchoEvent), IP, PORT, 2);
	listener->run();
	xq::net::release();
	printf("DONE...%lld\n", xq::tools::get_time_ms());
}