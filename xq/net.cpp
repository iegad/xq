#include "net.hpp"

// -------------------------------------------------------------------------------------- 公共函数 --------------------------------------------------------------------------------------

xq::net::SOCKET
xq::net::udp_socket(const char* local, const char* remote) {
    static constexpr int ON = 1;

    xq::net::SOCKET fd = INVALID_SOCKET;

    // 本端地址用于udp 套接字绑定
    if (local) {
        std::string tmp = std::move(std::string(local));
        const int pos = (int)tmp.rfind(':');
        if (pos == -1)
            return INVALID_SOCKET;

        std::string ip = std::move(tmp.substr(0, pos));
        if (ip.empty())
            ip = "0.0.0.0";

        std::string port = std::move(tmp.substr(pos + 1));

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
        std::string tmp = std::move(std::string(remote));
        const int pos = (int)tmp.rfind(':');
        if (pos == -1)
            return INVALID_SOCKET;

        std::string ip = std::move(tmp.substr(0, pos));
        if (ip.empty())
            return INVALID_SOCKET;

        std::string port = std::move(tmp.substr(pos + 1));

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

            if (!::connect(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
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
    }

    return n;
}

int 
xq::net::KcpConn::_set(uint32_t conv, const sockaddr* addr, int addrlen, int send_wnd, int recv_wnd, bool fast_mode) {
    bool rebuild = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (!kcp_ || ::memcmp(addr, &addr_, addrlen)) {
            if (kcp_) {
                ::ikcp_release(kcp_);
            }

            kcp_ = ::ikcp_create(conv, this);

            if (fast_mode)
                ::ikcp_nodelay(kcp_, 1, 20, 1, 1);
            else
                ::ikcp_nodelay(kcp_, 0, 20, 0, 0);

            assert(!::ikcp_wndsize(kcp_, send_wnd, recv_wnd) && "ikcp_wndsize called failed");
            assert(!::ikcp_setmtu(kcp_, KCP_MTU) && "ikcp_setmtu called failed");

            kcp_->output = udp_output;

            time_ = tools::get_time_ms();
            active_time_ = time_ / 1000;
            ::memcpy(&addr_, addr, addrlen);

            rebuild = true;
        }
    }

    return rebuild ? (event_->on_connected(this) == 0 ? 1 : -1) : 0;
}

int 
xq::net::KcpConn::_recv(SOCKET ufd, const sockaddr* addr, int addrlen, const char* raw, int raw_len, char* data, int data_len) {
    assert(raw && data && raw_len > 0 && data_len > 0);

    {// kcp locker
        std::lock_guard<std::mutex> lk(mtx_);

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
            std::lock_guard<std::mutex> lk(mtx_);
            if (!kcp_)
                return ERR_KCP_INVALID;

            n = ::ikcp_recv(kcp_, &data[0], MAX_DATA_SIZE);
        }


        if (n > 0) {
            if (event_->on_message(this, data, n)) {
                break;
            }
        }

        {// kcp locker
            std::lock_guard<std::mutex> lk(mtx_);

            if (!kcp_) {
                return ERR_KCP_INVALID;
            }

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

    update_thr_ = std::move(std::thread(std::bind(&KcpListener::update_thread, this)));

    for (uint32_t i = 0; i < nthread; i++)
        thread_pool_.emplace_back(std::thread(std::bind(&KcpListener::work_thread, this, host)));

    for (auto &t: thread_pool_)
        t.join();

    update_thr_.join();

    for (auto &conn: conn_map_)
        conn->_reset();

    state_ = State::Stopped;
}

void
xq::net::KcpListener::work_thread(const char* host) {
    const size_t max_conv = conn_map_.size();

    SOCKET ufd = udp_socket(host, nullptr);
    assert(ufd != INVALID_SOCKET);

    char rbuf[KCP_MTU] = {0};
    int n = 0;

    char* data = new char[MAX_DATA_SIZE];
    
    sockaddr addr;
    ::memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);

    uint32_t conv = 0;
    KcpConn::Ptr conn;

    while (state_ == State::Running) {
        n = ::recvfrom(ufd, rbuf, KCP_MTU, 0, &addr, &addrlen);
        if (n <= 0) {
            continue;
        }

        if (n < KCP_HEAD_SIZE) {
            continue;
        }

        conv = *rbuf;
        if (conv > 0 && conv <= max_conv) {
            conn = conn_map_[conv - 1];
            if (conn->_set(conv, &addr, addrlen) < 0)
                conn->_reset();

            if (conn->_recv(ufd, &addr, addrlen, rbuf, n, data, MAX_DATA_SIZE) < 0)
                conn->_reset();
        }
    }

    xq::net::close(ufd);
    delete[] data;
}

void 
xq::net::KcpListener::update_thread() {
    uint64_t now_ms = 0, pre_ms = tools::get_time_ms();

    while (state_ == State::Running) {    
        now_ms = tools::get_time_ms();
        if (now_ms - pre_ms > 0) {
            for (auto &conn : conn_map_) {
                if (conn->update(now_ms))
                    conn->_reset();
            }
        }
        pre_ms = now_ms;
    }
}
