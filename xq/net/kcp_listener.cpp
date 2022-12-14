#include "net/kcp_listener.hpp"
#include <string>
#include <spdlog/spdlog.h>

void 
xq::net::KcpListener::run() {
	update_thr_ = std::thread(std::bind(&KcpListener::_update_thr, this));

	state_ = State::Runing;

	for (uint32_t i = 0; i < nthread_; i++) {
		SOCKET sockfd = udp_socket(host_.c_str(), nullptr);
		assert(sockfd != INVALID_SOCKET);
		ufds_.emplace_back(sockfd);

		recv_pool_.emplace_back(std::thread(std::bind(&KcpListener::_send_thr, this, sockfd)));
		send_pool_.emplace_back(std::thread(std::bind(&KcpListener::_recv_thr, this, sockfd)));
	}

	update_thr_.join();

	for (uint32_t i = 0; i < nthread_; i++) {
		std::thread& rt = recv_pool_[i];
		rt.join();

		std::thread& st = send_pool_[i];
		st.join();
	}

	for (auto ufd : ufds_)
		close(ufd);

	recv_pool_.clear();
	send_pool_.clear();
	state_ = State::Stopped;
}

void 
xq::net::KcpListener::stop() {
	state_ = State::Stopping;
	for (auto ufd : ufds_)
		close(ufd);

	ufds_.clear();
}

int 
xq::net::KcpListener::_udp_output(const char* data, int datalen, IKCPCB* kcp, void* user) {
	KcpSess* s = (KcpSess*)user;
	auto addr = s->addr();
	KcpSeg::Ptr seg = KcpSeg::create((uint8_t*)data, datalen, addr.first, addr.second);
	s->que().enqueue(seg);
	return 0;
}

void
xq::net::KcpListener::_update_thr() {
	int64_t now_ms;

	while (State::Runing == state_) {
		std::this_thread::sleep_for(std::chrono::milliseconds(xq::net::KCP_UPDATE_MS));
		now_ms = xq::tools::now_milli();
		for (auto itr = sess_map_.begin(); itr != sess_map_.end(); ++itr) {
			auto& s = itr->second;

			if (s->update(now_ms, timeout_) < 0) {
				remove_sess(itr++);
			}
		}
	}
}

#ifdef _WIN32
void 
xq::net::KcpListener::_recv_thr(SOCKET sockfd) {
	int n;
	uint8_t rbuf[KCP_MTU];
	uint8_t* data = new uint8_t[KCP_MAX_DATA_SIZE];
	size_t datalen;

	sockaddr addr;
	socklen_t addrlen = sizeof(addr);
	::memset(&addr, 0, sizeof(addr));

	uint32_t conv;
	KcpSess* sess;
	KcpSess::Ptr nconn;
	xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator itr;
	std::pair<xq::tools::Map<uint32_t, KcpSess::Ptr>::iterator, bool> pair;

	while (State::Runing == state_) {
		n = ::recvfrom(sockfd, (char *)rbuf, KCP_MTU, 0, &addr, &addrlen);
		if (n <= 0) {
			event_->on_error(ErrType::ET_ListenerRead, this, error());
			continue;
		}

		event_->on_recv(sockfd, &addr, addrlen, rbuf, n);

		if (n < KCP_HEAD_SIZE) {
			event_->on_error(ErrType::ET_SessRead, &addr, addrlen);
			continue;
		}

		conv = *(uint32_t*)rbuf;
		itr = sess_map_.find(conv);
		if (itr == sess_map_.end()) {
			nconn = KcpSess::create(que_, conv, timeout_, &KcpListener::_udp_output);
			if (event_->on_connected(nconn.get()) < 0)
				continue;

			pair = add_sess(conv, nconn);
			assert(pair.second);
			itr = pair.first;
		}
		sess = itr->second.get();
		if (sess->change(sockfd, &addr, addrlen)) {
			// TODO: ÖØÁ¬
		}

		if (sess->input(rbuf, n) < 0) {
			event_->on_error(ErrType::ET_SessRead, &addr, addrlen);
			remove_sess(itr);
			continue;
		}

		datalen = KCP_MAX_DATA_SIZE;
		n = sess->recv(data, datalen);
		if (n <= 0)
			continue;

		sess->flush();

		if (event_->on_message(sess, data, n) < 0)
			remove_sess(itr);
	}
}

void 
xq::net::KcpListener::_send_thr(SOCKET sockfd) {
	int n;
	KcpSeg::Ptr seg;
	while (State::Runing == state_) {
		que_.wait_dequeue(seg);
		event_->on_send(sockfd, &seg->addr, seg->addrlen, &seg->data[0], seg->data.size());
		n = ::sendto(sockfd, (char*)&seg->data[0], (int)seg->data.size(), 0, &seg->addr, seg->addrlen);
		if (n <= 0)
			event_->on_error(ErrType::ET_ListenerWrite, this, error());
		seg.reset();
	}
}
#else 

#endif // !_WIN32
