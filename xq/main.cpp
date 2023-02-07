#include "net/kcp_listener.hpp"
#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif

class EchoEvent : public xq::net::KcpListener::IEvent {
public:
	virtual int on_message(xq::net::KcpSess* sess, const uint8_t* data, size_t datalen) override {
		return sess->send(data, datalen);
	}
};

constexpr char HOST[] = ":6688";

int
main(int, char **) {
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
