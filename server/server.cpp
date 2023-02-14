#include "xq/tools/tools.hpp"
#include "xq/net//kcp_listener.hpp"

class EchoEvent;
typedef xq::net::KcpListener<EchoEvent> KcpListener;
typedef xq::net::KcpSess<EchoEvent> KcpSess;


class EchoEvent {
public:
	int on_message(KcpSess* sess, const uint8_t* data, size_t datalen) {
		return sess->send(data, datalen);
	}

	int on_connected(KcpSess* sess)  {
		std::printf("+++ %u has connected\n", sess->get_conv());
		return 0;
	}

	int on_reconnected(KcpSess* sess) {
		std::printf("### %u has reconnected\n", sess->get_conv());
		return 0;
	}

	void on_disconnected(KcpSess * sess) {
		std::printf("--- %u has disconnected\n", sess->get_conv());
	}

	void on_error(xq::net::ErrType err_type, int err, void* arg) {

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

	KcpListener::Ptr listener = KcpListener::create(HOST, 2000);
	listener->run();

#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
	exit(0);
}
