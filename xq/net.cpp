#include "net.hpp"

// -------------------------------------------------------------------------------------- 公共函数 --------------------------------------------------------------------------------------

xq::net::SOCKET
xq::net::udp_socket(const char *ip, const char *svc, bool is_server) {
    static const int ON = 1;

    xq::net::SOCKET fd = 0;

    if (ip && svc) {
        addrinfo hints;
        addrinfo* result = nullptr, * rp = nullptr;    

        ::memset(&hints, 0, sizeof(addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (::getaddrinfo(ip, svc, &hints, &result)) {
            return -1;
        }

        for (rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                continue;
            }

            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&ON, sizeof(int))) {
                return -1;
            }

#ifndef _WIN32 
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char*)&ON, sizeof(int))) {
                return -3;
            }
#endif
            if (is_server) {
                if (!::bind(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
                    break;
                }
            }
            else {
                if (!::connect(fd, rp->ai_addr, (int)rp->ai_addrlen)) {
                    break;
                }
            }

            xq::net::close(fd);
        }

        assert(rp);

        ::freeaddrinfo(result);

        if (fd <= 0) {
            return -1;
        }
    }
    else {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    }

    return fd;
}


// -------------------------------------------------------------------------------------- KcpConn --------------------------------------------------------------------------------------

xq::net::KcpConn::Ptr 
xq::net::KcpConn::create(IEvent::Ptr event) {
    return Ptr(new KcpConn(event));
}

xq::net::KcpConn::Ptr
xq::net::KcpConn::connect(IEvent::Ptr event, const char* ip, const char* svc, uint32_t conv, int send_wnd, int recv_wnd, bool fast_mode) {
    return Ptr(new KcpConn(event, ip, svc, conv, send_wnd, recv_wnd, fast_mode));
}

int
xq::net::KcpConn::udp_output(const char* data, int datalen, IKCPCB*, void* conn) {
    KcpConn* self = (KcpConn*)conn;

    self->event_->on_send(self, data, datalen);
    int n = ::sendto(self->ufd_, data, datalen, 0, &self->addr_, self->addrlen_);
    if (n < 0) {
        printf("sendto callied failed: %d\n", xq::net::error());
    }

    return n;
}

int 
xq::net::KcpConn::_set(uint32_t conv, const sockaddr* addr, int addrlen, int send_wnd, int recv_wnd, bool fast_mode) {
    if (active() && ::memcmp(addr, &addr_, addrlen)) {
        kcp_release();
        time_ = tools::get_time_ms();
        active_time_ = time_ / 1000;
        ::memcpy(&addr_, addr, addrlen);
        kcp_create(conv, send_wnd, recv_wnd, fast_mode);
        return event_->on_connected(this);
    }

    return 0;
}

void 
xq::net::KcpConn::_reset() {
    std::unique_lock<std::mutex> lk(mtx_);

    if (ufd_ != INVALID_SOCKET) {
        ufd_ = INVALID_SOCKET;
    }

    if (kcp_) {
        event_->on_disconnected(this);
        ikcp_release(kcp_);
        kcp_ = nullptr;
    }

    time_ = active_time_ = 0;
}

int 
xq::net::KcpConn::update(uint64_t now_ms, int timeout) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (kcp_) {
        if (now_ms / 1000 - active_time_ > timeout) {
            return -1;
        }

        ::ikcp_update(kcp_, (uint32_t)(now_ms - time_));
        return 0;
    }

    return 1;
}

int 
xq::net::KcpConn::_recv(SOCKET ufd, const sockaddr* addr, int addrlen, const char* raw, int rawlen, char* data, int datalen) {
    {
        std::unique_lock<std::mutex> lk(mtx_);
        if (kcp_) {
            if (::ikcp_input(kcp_, raw, rawlen)) {
                return -1;
            }
        }
    }

    active_time_ = ::time(nullptr);
    _set_remote(ufd, addr, addrlen);

    int n;
    do {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            n = ::ikcp_recv(kcp_, data, datalen);
        }

        if (n <= 0) {
            return 0;
        }
        else {
            if (n > 0) {
                if (event_->on_message(this, data, n)) {
                    return -1;
                }
            }
        }

        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!kcp_->nrcv_que) {
                break;
            }
        }
    } while (1);
}

int 
xq::net::KcpConn::send(const char* data, int datalen) {
    std::unique_lock<std::mutex> lk(mtx_);
    if (ufd_ == INVALID_SOCKET || !kcp_)
        return -1;

    return ::ikcp_send(kcp_, data, datalen);
}

void
xq::net::KcpConn::_set_remote(SOCKET ufd, const sockaddr* addr, int addrlen) {
    if (ufd_ != ufd || ufd_ == INVALID_SOCKET) {
        ufd_ = ufd;
    }

    if (!::memcmp(&addr_, addr, addrlen)) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }
}

xq::net::KcpConn::KcpConn(IEvent::Ptr event) :
    kcp_(nullptr),
    addrlen_(0),
    ufd_(INVALID_SOCKET),
    time_(0),
    active_time_(0),
    event_(event) {
    ::memset(&addr_, 0, sizeof(addr_));
}

xq::net::KcpConn::KcpConn(IEvent::Ptr event, const char* ip, const char* svc, uint32_t conv, int send_wnd, int recv_wnd, bool fast_mod) :
    kcp_(nullptr), 
    addrlen_(0),
    ufd_(INVALID_SOCKET),
    time_(xq::tools::get_time_ms()),
    active_time_(time_ / 1000),
    event_(event) {
    ::memset(&addr_, 0, sizeof(addr_));

    ufd_ = udp_socket(ip, svc, false);
    assert(ufd_ != INVALID_SOCKET);

    
    if (!conv) {
        ::srand((int)time_);
        conv = ::rand();
    }

    kcp_create(conv, send_wnd, recv_wnd, fast_mod);
}

// -------------------------------------------------------------------------------------- KCP Listener --------------------------------------------------------------------------------------

xq::net::KcpListener::ptr 
xq::net::KcpListener::create(IEvent::Ptr event, const char* ip, const char* port, int nthread) {
    assert(nthread >= 0);

    if (nthread == 0) {
        nthread = (int)std::thread::hardware_concurrency();
    }

    return ptr(new KcpListener(event, ip, port, nthread, nthread * 1000));
}

void 
xq::net::KcpListener::run() {
    for (auto itr = thread_pool_.begin(); itr != thread_pool_.end(); ++itr) {
        itr->join();
    }
}

xq::net::KcpListener::KcpListener(IEvent::Ptr event, const char* ip, const char* port, int nthread, int max_conn) :
    state_(State::Stopped) {
    state_ = State::Running;

    std::thread(std::bind(&KcpListener::update_thread, this)).detach();

    for (int i = 1; i <= max_conn; i++) {
        sess_map_.emplace(std::make_pair(i, KcpConn::create(event)));
    }

    for (int i = 0; i < nthread; i++) {
        thread_pool_.emplace_back(std::thread(std::bind(&KcpListener::work_thread, this, ip, port)));
    }
}

void
xq::net::KcpListener::work_thread(const char* ip, const char* port) {
    SOCKET ufd = udp_socket(ip, port);

    char rbuf[KCP_MTU];
    char* data = new char[MAX_DATA_SIZE];
    int n = 0;
    
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
        conn = sess_map_[conv];
        if (conn->_set(conv, &addr, addrlen)) {
            conn->_reset();
        }

        if (conn->_recv(ufd, &addr, addrlen, rbuf, n, data, MAX_DATA_SIZE)) {
            conn->_reset();
        }
    }

    delete[] data;
    xq::net::close(ufd);
}

void 
xq::net::KcpListener::update_thread() {
    while (1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        uint64_t now_ms = tools::get_time_ms();

        for (auto itr = sess_map_.begin(); itr != sess_map_.end(); ++itr) {
            if (itr->second->update(now_ms, 30) == -1) {
                printf("%d has timeout\n", itr->first);
                itr->second->_reset();
            }
        }
    }
}
