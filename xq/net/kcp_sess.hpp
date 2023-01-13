#ifndef __KCP_HPP__
#define __KCP_HPP__

#include "net/kcp.hpp"

namespace xq {
namespace net {

struct KcpSeg {
	typedef xq::tools::ObjectPool<KcpSeg> Pool;

	uint8_t* data;
	size_t len;
	sockaddr addr;
	socklen_t addrlen;

	KcpSeg()
		: data(new uint8_t[KCP_MAX_DATA_SIZE])
		, len(KCP_MAX_DATA_SIZE)
		, addr({ {0},0 })
		, addrlen(sizeof(addr)) {
		assert(data);
	}

	~KcpSeg() {
		delete[] data;
	}
}; // struct KcpSeg;

class KcpSess: public Kcp {
public:
	typedef std::shared_ptr<KcpSess>  Ptr;

	static Ptr create(uint32_t conv) {
		return Ptr(new KcpSess(conv));
	}

	std::pair<sockaddr*, socklen_t> addr() {
		return std::make_pair(&addr_, addrlen_);
	}

	void set_ufd(SOCKET ufd) {
		if (ufd_ != ufd) {
			ufd_ = ufd;
		}
	}

	uint32_t ufd() {
		return ufd_;
	}

private:
	KcpSess(uint32_t conv)
		: Kcp(conv, this)
		, ufd_(INVALID_SOCKET)
		, addr_({ {0}, 0 })
		, addrlen_(sizeof(addr_)) {
	}

	SOCKET ufd_;
	sockaddr addr_;
	size_t addrlen_;

    KcpSess(const KcpSess&);
    KcpSess& operator=(const KcpSess&);
}; // class KcpSess;

} // namespace net
} // namespace xq

#endif // __KCP_HPP__
