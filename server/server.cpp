#include "xq/net.hpp"
#include "xq/tools.hpp"

constexpr char HOST[] = "0.0.0.0:6688";

class EchoEvent : public virtual xq::net::IEvent {
public:
	typedef std::shared_ptr<EchoEvent> Ptr;

	virtual int on_message(xq::net::KcpConn* conn, const char* data, size_t data_len) override {
		printf("%d -> %s\n", conn->conv(), std::string(data, data_len).c_str());
		return conn->send(data, data_len);
	}

	virtual int on_connected(xq::net::KcpConn* conn) override {
		printf("%d has connected\n", conn->conv());
		return 0;
	}

	virtual void on_disconnected(xq::net::KcpConn* conn) override {
		printf("%d has disconnected\n", conn->conv());
	}
};

int
main(int argc, char** argv) {
#ifdef _WIN32
	WSADATA wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202)
		exit(EXIT_FAILURE);
#endif // _WIN32

	auto listener = xq::net::KcpListener::create(2);
	listener->run(xq::net::IEvent::Ptr(new EchoEvent), HOST);

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
}