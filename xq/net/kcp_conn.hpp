#ifndef __KCP_CONN__
#define __KCP_CONN__

#include "net/kcp.hpp"

namespace xq {
namespace net {

class KcpConn final {
public:
    typedef std::shared_ptr<KcpConn> Ptr;

    static Ptr create(const std::string& host, const std::string& local = "") {

    }

private:
    KcpConn(const std::string &host, const std::string &local) {

    }

    SOCKET ufd_;
    sockaddr raddr_;
    socklen_t raddrlen_;
    sockaddr laddr_;
    socklen_t laddrlen_;
}; // class KcpConn;


} // namespace net
} // namespace xq

#endif // !__KCP_CONN__
