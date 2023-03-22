#include "xq/net/udp_session.hpp"
#include "xq/net/udx.hpp"


using UdpSession = xq::net::UdpSession;
using Udx = xq::net::Udx;


static UdpSession::Ptr sess;
static Udx::Ptr udx;


int rcv_cb(UdpSession::Segment::Ptr &udp_seg) {
	int n = udx->input(udp_seg->data, udp_seg->datalen, udp_seg->time_ms);
	if (n < 0) {
		std::printf("%d\n", n);
	}

	return 0;
}


void send_wkr() {
	udx->set_addr("1.15.81.179:6688");
	char buf[xq::net::UDX_MSS] = { 0 };

	for (int i = 0; i < 1; i++) {
		sprintf(buf, "Hello world: %d", i);
		udx->send((uint8_t*)buf, strlen(buf));
		udx->flush(xq::tools::now_ms());
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}



int main(int argc, char** argv) {
	WSADATA wdata;
	if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) {
		exit(EXIT_FAILURE);
	}

	sess = UdpSession::create();

	std::thread(send_wkr).detach();

	udx = Udx::create(1, sess);
	sess->start_rcv(rcv_cb);

	WSACleanup();
	exit(EXIT_SUCCESS);
}