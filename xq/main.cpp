#include "net/kcp_listener.hpp"
#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif

constexpr char HOST[] = ":6688";

class EchoEvent {
public:
	typedef xq::net::KcpListener<EchoEvent> Server;
	typedef xq::net::KcpSess Session;

	EchoEvent() {}
	EchoEvent(const EchoEvent&) = delete;
	EchoEvent& operator=(const EchoEvent&) = delete;

    int on_inited(Server* ) { return 0; }
    void on_stopped(Server* ) {}
    int on_connected(Session* ) { return 0; }
    void on_disconnected(Session* ) {}
    void on_error(xq::net::ErrType, void*, int) {}
    void on_recv(xq::net::SOCKET, const sockaddr*, socklen_t, const uint8_t*, size_t) {}
    void on_send(xq::net::SOCKET, const sockaddr*, socklen_t, const uint8_t*, size_t) {}
    int on_message(xq::net::KcpSess* s, const uint8_t* data, size_t datalen) {
		return s->send(data, datalen);
	}
};

int
main(int, char **) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32

	auto server = xq::net::KcpListener<EchoEvent>::create(HOST, xq::net::KCP_DEFAULT_TIMEOUT, 4);
	server->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
