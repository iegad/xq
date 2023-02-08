#include "xq/tools/tools.hpp"
#include "xq/net//kcp_listener.hpp"


class EchoEvent : public xq::net::KcpListener::IEvent {
public:
	virtual int on_message(xq::net::KcpSess* sess, const uint8_t* data, size_t datalen) override {
		return sess->send(data, datalen);
	}

	virtual int on_connected(xq::net::KcpSess* sess) override  {
		std::printf("+++ %u has connected\n", sess->get_conv());
		return 0;
	}

	virtual int on_reconnected(xq::net::KcpSess* sess) override {
		std::printf("### %u has reconnected\n", sess->get_conv());
		return 0;
	}

	virtual void on_disconnected(xq::net::KcpSess* sess) override {
		std::printf("--- %u has disconnected\n", sess->get_conv());
	}
};

constexpr char HOST[] = ":6688";

int
main(int, char**) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32

	EchoEvent ev;

	xq::net::KcpListener::Ptr listener = xq::net::KcpListener::create(&ev, HOST, 1000);
	listener->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
