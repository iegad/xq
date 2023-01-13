#include "net/kcp_listener.hpp"
#ifndef _WIN32
#include <jemalloc/jemalloc.h>
#endif

constexpr char HOST[] = ":6688";

int
main(int, char **) {
#ifdef _WIN32
	WSAData wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wVersion != 0x0202)
		exit(1);
#endif // _WIN32

	xq::net::KcpListener::Ptr listener = xq::net::KcpListener::create(HOST, 1000);
	listener->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
