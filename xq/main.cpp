#include "net/kcp_listener.hpp"

constexpr char HOST[] = ":6688";

class EchoEvent : public xq::net::IListenerEvent {
public:
	// Í¨¹ý IListenerEvent ¼Ì³Ð
	virtual int on_message(xq::net::KcpSess* s, const uint8_t* data, size_t datalen) override {
		return s->send(data, datalen);
	}
};

int
main(int argc, char **argv) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32

	auto server = xq::net::KcpListener::create(xq::net::IListenerEvent::Ptr(new EchoEvent), HOST, xq::net::KCP_DEFAULT_TIMEOUT, 4);
	server->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}