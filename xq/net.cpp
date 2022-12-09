#include "net.hpp"
#include "tools.hpp"

// -------------------------------------------------------------------------------------- 公共函数 --------------------------------------------------------------------------------------

xq::net::SOCKET
xq::net::udp_socket(const char* local, const char* remote, sockaddr *addr, socklen_t *addrlen) {
    static constexpr int ON = 1;

    xq::net::SOCKET fd = INVALID_SOCKET;

    // 本端地址用于udp 套接字绑定
    if (local) {
        std::string tmp = std::string(local);
        const int pos = (int)tmp.rfind(':');
        if (pos == -1)
            return INVALID_SOCKET;

        std::string ip = tmp.substr(0, pos);
        if (ip.empty())
            ip = "0.0.0.0";

        std::string port = tmp.substr(pos + 1);

        addrinfo hints;
        addrinfo* result = nullptr, * rp = nullptr;

        ::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result)) {
            return -1;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                continue;
            }

            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
                return INVALID_SOCKET;
            }

#ifndef _WIN32 
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
                return INVALID_SOCKET;
            }
#endif

            if (!::bind(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
                break;
            }

            xq::net::close(fd);
        }

        assert(rp);

        ::freeaddrinfo(result);

        if (fd <= 0) {
            return INVALID_SOCKET;
        }
    }

    // 对端地址用于连接
    if (remote) {
        std::string tmp = std::string(remote);
        const int pos = (int)tmp.rfind(':');
        if (pos == -1)
            return INVALID_SOCKET;

        std::string ip = tmp.substr(0, pos);
        if (ip.empty())
            return INVALID_SOCKET;

        std::string port = tmp.substr(pos + 1);

        addrinfo hints;
        addrinfo* result = nullptr, * rp = nullptr;

        ::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result)) {
            return -1;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            if (fd == INVALID_SOCKET) {
                fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (fd < 0) {
                    continue;
                }

                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&ON, sizeof(int))) {
                    return INVALID_SOCKET;
                }

#ifndef _WIN32 
                if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &ON, sizeof(int))) {
                    return INVALID_SOCKET;
                }
#endif
            }

            if (!::connect(fd, rp->ai_addr, rp->ai_addrlen)) {
                assert(addr && addrlen);
                *addrlen = rp->ai_addrlen;
                ::memcpy(addr, rp->ai_addr, rp->ai_addrlen);
                break;
            }

            xq::net::close(fd);
        }

        assert(rp);

        ::freeaddrinfo(result);

        if (fd <= 0) {
            return INVALID_SOCKET;
        }
    }

    return fd;
}


// -------------------------------------------------------------------------------------- KcpConn --------------------------------------------------------------------------------------

int
xq::net::KcpConn::udp_output(const char* data, int datalen, IKCPCB*, void* conn) {
    KcpConn* self = (KcpConn*)conn;

    self->event_->on_send(self, data, datalen);
    const int n = ::sendto(self->ufd_, data, datalen, 0, &self->addr_, self->addrlen_);
    if (n < 0) {
        // TODO: log...
        printf("send failed: %d\n", error());
    }

    return n;
}

xq::net::KcpConn::KcpConn(IEvent::Ptr event, const char* local, const char* remote, uint32_t conv) :
    ufd_(INVALID_SOCKET),
    addrlen_(sizeof(sockaddr)),
    kcp_(nullptr),
    timeout_(DEFAULT_TIMEOUT),
    time_(xq::tools::get_time_ms()),
    active_time_(time_ / 1000),
    event_(event) {
    ::memset(&addr_, 0, sizeof(addr_));

    ufd_ = udp_socket(local, remote, &addr_, &addrlen_);
    assert(ufd_ != INVALID_SOCKET && conv > 0);

    kcp_ = ::ikcp_create(conv, this);
    assert(kcp_);
    kcp_->output = udp_output;
    set();
}

int 
xq::net::KcpConn::recv() {
    int n = 0;
    char rbuf[KCP_MTU];
    std::vector<char> data(MAX_DATA_SIZE);
    sockaddr addr;
    socklen_t addrlen = sizeof(addr);
    ::memset(&addr, 0, sizeof(addr));

    n = ::recvfrom(ufd_, rbuf, KCP_MTU, 0, &addr, &addrlen);
    if (n <= 0) {
        printf("recvfrom failed: %d\n", error());
        return -1;
    }

    if (n < 24) {
        printf("kcp recv failed\n");
        return -2;
    }

    if (_recv(ufd_,kcp_->conv, &addr, addrlen, rbuf, n, &data[0], MAX_DATA_SIZE) < 0)
        return -3;

    return 0;
}

int
xq::net::KcpConn::_recv(SOCKET ufd, uint32_t conv, const sockaddr* addr, int addrlen, const char* raw, int raw_len, char* data, int data_len) {
    assert(raw && data && raw_len > 0 && data_len > 0);
    
    if (!active() || ::memcmp(addr, &addr_, addrlen)) {
        {
            std::lock_guard<xq::tools::SpinMutex> lk(mtx_);

            if (kcp_)
                ::ikcp_release(kcp_);

            kcp_ = ::ikcp_create(conv, this);
            assert(kcp_);
            assert(!::ikcp_nodelay(kcp_, 1, 20, 1, 1));
            assert(!::ikcp_wndsize(kcp_, DEFAULT_SEND_WND, DEFAULT_SEND_WND) && "ikcp_wndsize called failed");
            assert(!::ikcp_setmtu(kcp_, KCP_MTU) && "ikcp_setmtu called failed");
            kcp_->output = udp_output;

            time_ = tools::get_time_ms();
            active_time_ = time_ / 1000;
            ::memcpy(&addr_, addr, addrlen);
        }

        if (event_->on_connected(this))
            return -1;
    }

    {// kcp locker
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);

        if (!kcp_)
            return ERR_KCP_INVALID;

        if (::ikcp_input(kcp_, raw, raw_len))
            return -1;
    }

    active_time_ = ::time(nullptr);
    _set_remote(ufd, addr, addrlen);

    int n;
    do {
        {// kcp locker
            std::lock_guard<xq::tools::SpinMutex> lk(mtx_);
            if (!kcp_)
                return ERR_KCP_INVALID;

            if (n = ::ikcp_recv(kcp_, &data[0], MAX_DATA_SIZE), n < 0)
                break;
        }

        if (n > 0 && event_->on_message(this, data, n) < 0) {
            return -2;
        }

        if (!active()) {
            return ERR_KCP_INVALID;
        }

        {
            std::lock_guard<xq::tools::SpinMutex> lk(mtx_);
            if (!kcp_->nrcv_que)
                break;
        }
    } while (1);

    return 0;
}

// -------------------------------------------------------------------------------------- KCP Listener --------------------------------------------------------------------------------------

void 
xq::net::KcpListener::run(IEvent::Ptr event, const char* host, uint32_t nthread, uint32_t max_conn) {
    assert(max_conn > 0);

    if (!nthread)
        nthread = std::thread::hardware_concurrency();

    state_ = State::Running;

    for (uint32_t i = 1; i <= max_conn; i++)
        conn_map_.emplace_back(KcpConn::create(event, timeout_));

    update_thr_ = std::thread(std::bind(&KcpListener::update_thread, this));

    for (uint32_t i = 0; i < nthread; i++)
        thread_pool_.emplace_back(std::thread(std::bind(&KcpListener::work_thread, this, host)));

    for (auto &t: thread_pool_)
        t.join();

    update_thr_.join();

    for (auto &conn: conn_map_)
        conn->_reset();

    state_ = State::Stopped;
}

#ifdef _WIN32
void
xq::net::KcpListener::work_thread(const char* host) {
    const size_t max_conv = conn_map_.size();

    SOCKET ufd = udp_socket(host, nullptr);
    assert(ufd != INVALID_SOCKET);

    char buf[KCP_MTU];
    int n = 0;

    std::vector<char> data(MAX_DATA_SIZE);

    sockaddr addr;
    ::memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);

    uint32_t conv = 0;
    KcpConn::Ptr conn;

    while (state_ == State::Running) {
        n = ::recvfrom(ufd, buf, KCP_MTU, 0, &addr, &addrlen);
        if (n <= 0) {
            continue;
        }

        if (n < KCP_HEAD_SIZE) {
            continue;
        }

        conv = tools::to_le(*((uint32_t*)buf));
        if (conv > 0 && conv <= max_conv) {
            conn = conn_map_[conv - 1];
            if (conn->_recv(ufd, conv, &addr, addrlen, buf, n, &data[0], MAX_DATA_SIZE) < 0)
                conn->_reset();
        }
    }

    xq::net::close(ufd);
}

#else // 类UNIX平台使用recvmmsg 以减少系统调用.

void
xq::net::KcpListener::work_thread(const char* host) {
    static constexpr int MSG_LEN = 64;
    const size_t max_conv = conn_map_.size();

    SOCKET ufd = udp_socket(host, nullptr);
    assert(ufd != INVALID_SOCKET);

    int i, n = 0;

    sockaddr addrs[MSG_LEN];
    ::memset(&addrs, 0, sizeof(addrs));

    char bufs[MSG_LEN][KCP_MTU];

    mmsghdr msgs[MSG_LEN];
    ::memset(msgs, 0, sizeof(msgs));

    iovec iovecs[MSG_LEN];
    for (i = 0; i < MSG_LEN; i++) {
        iovecs[i].iov_base = bufs[i];
        iovecs[i].iov_len = KCP_MTU;
        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    uint32_t conv;
    KcpConn::Ptr conn;
    std::vector<char> data(MAX_DATA_SIZE);

    size_t buflen;
    socklen_t addrlen;
    sockaddr* addr = nullptr;
    char* buf = nullptr;

    while (state_ == State::Running) {
        if (n = ::recvmmsg(ufd, msgs, MSG_LEN, MSG_WAITFORONE, nullptr), n < 0) {
            printf("recvmmsg error: %d\n", error());
            continue;
        }

        for (i = 0; i < n; i++) {
            buf = bufs[i];
            buflen = msgs[i].msg_len;
            if (buflen < KCP_HEAD_SIZE)
                continue;

            conv = tools::to_le(*((uint32_t*)buf));
            if (conv > 0 && conv <= max_conv) {
                addrlen = msgs[i].msg_hdr.msg_namelen;
                addr = &addrs[i];

                conn = conn_map_[conv - 1];
                if (conn->_recv(ufd, conv, addr, addrlen, buf, buflen, &data[0], MAX_DATA_SIZE) < 0)
                    conn->_reset();
            }
        }
    }

    xq::net::close(ufd);
}
#endif // !_WIN32

void 
xq::net::KcpListener::update_thread() {
    uint64_t now_ms;

    while (state_ == State::Running) {    
        std::this_thread::sleep_for(std::chrono::microseconds(999));
        now_ms = tools::get_time_ms();
        for (auto& conn : conn_map_) {
            if (conn->update(now_ms))
                conn->_reset();
        }
    }
}
