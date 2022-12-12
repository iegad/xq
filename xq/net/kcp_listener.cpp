#include "net/kcp_listener.hpp"
#include <string>

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
	while (State::Runing == state_) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		
		for (auto itr = sess_map_.begin(); itr != sess_map_.end(); ++itr) {
			auto& s = itr->second;
			if (s->update() < 0) {
				s->release();
				sess_map_.erase(itr++);
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
			pair = sess_map_.insert(conv, KcpSess::create(que_, conv, timeout_, &KcpListener::_udp_output));
			assert(pair.second);
			itr = pair.first;
			if (event_->on_connected(itr->second.get()) < 0) {
				sess_map_.erase(itr);
				continue;
			}
		}
		sess = itr->second.get();

		if (sess->input(rbuf, n) < 0) {
			event_->on_error(ErrType::ET_SessRead, &addr, addrlen);
			sess_map_.erase(itr);
			continue;
		}

		datalen = KCP_MAX_DATA_SIZE;
		n = sess->recv(data, datalen);
		if (n <= 0)
			continue;

		sess->set_remote(sockfd, &addr, addrlen);
		sess->update();

		if (event_->on_message(sess, data, n) < 0)
			sess_map_.erase(itr);
	}
}

void 
xq::net::KcpListener::_send_thr(SOCKET sockfd) {
	KcpSeg::Ptr seg;
	int n;

	while (State::Runing == state_) {
		if (que_.wait_dequeue_timed(seg, std::chrono::milliseconds(5))) {
			n = ::sendto(sockfd, (char*)seg->data, (int)seg->len, 0, &seg->addr, seg->addrlen);
			if (n <= 0) {
				event_->on_error(ErrType::ET_ListenerWrite, this, error());
				continue;
			}
			event_->on_send(sockfd, &seg->addr, seg->addrlen, seg->data, seg->len);
		}
	}
}
#else 

#endif // !_WIN32
