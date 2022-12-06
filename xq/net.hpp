#ifndef __NET_HPP__
#define __NET_HPP__

//! --------------------------------------------------------------------------------------------------------------------
//! xq 网络框架, 目前只支持KCP版本.
//! 
//! @author:      iegad
//! @create at:   2022-12-06
//! @update:
//! --------------------------------------------------------------------------------------------------------------------
//! |- time                |- coder                  |- content 

// -------------------------------------------------------------------------------------- system --------------------------------------------------------------------------------------
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

// -------------------------------------------------------------------------------------- C --------------------------------------------------------------------------------------
#include <assert.h>
#include <errno.h>
#include <memory.h>

// -------------------------------------------------------------------------------------- C++ --------------------------------------------------------------------------------------
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// -------------------------------------------------------------------------------------- 3rd party --------------------------------------------------------------------------------------
#include "ikcp.h"

// -------------------------------------------------------------------------------------- xq --------------------------------------------------------------------------------------
#include "tools.hpp"

namespace xq {
namespace net {

// -------------------------------------------------------------------------------------- 类型/符号 --------------------------------------------------------------------------------------
// ====================
// SOCKET 类型
// ====================
#ifdef _WIN32
    typedef SOCKET SOCKET;
#else
    typedef int SOCKET;
#endif

// ====================
// 无效的SOCKET 符号
// ====================
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (SOCKET)(~0)
#endif // !INVALID_SOCKET

// -------------------------------------------------------------------------------------- 常量定义 --------------------------------------------------------------------------------------
const int DEFAULT_SEND_WND = 256;   // 发送窗口大小
const int DEFAULT_RECV_WND = 256;   // 接收窗口大小
const int DEFAULT_TIMEOUT  = 60;    // 默认超时(秒)

const int KCP_MTU       = 1472;                             // KCP MTU
const int KCP_HEAD_SIZE = 24;                               // KCP消息头长度
const int MAX_DATA_SIZE = 128 * (KCP_MTU - KCP_HEAD_SIZE);  // 消息包最大长度: 181(kB)

// -------------------------------------------------------------------------------------- 公共函数 --------------------------------------------------------------------------------------

/// <summary>
/// 初始化网络资源, 用于WinSock.
/// </summary>
/// <returns>成功返回0, 否则返回-1</returns>
inline int init() {
#ifdef _WIN32
    WSADATA wdata;
    if (WSAStartup(0x0202, &wdata) || wdata.wHighVersion != 0x0202) { return -1; }
#endif
    return 0;
}

/// <summary>
/// 释放网络资源, 用于WinSock.
/// </summary>
inline void release() {
#ifdef _WIN32
    WSACleanup();
#endif
}

/// <summary>
/// 关闭套接字
/// </summary>
/// <param name="sockfd">需要关闭的套接字</param>
/// <returns>成功返回0, 否则返回-1</returns>
inline int close(SOCKET sockfd) {
#ifndef _WIN32
    return ::close(sockfd);
#else
    return ::closesocket(sockfd);
#endif
}

/// <summary>
/// 获取socket 最后一次错误码.
/// </summary>
/// <returns>错误码</returns>
inline int error() {
#ifndef _WIN32
    return errno;
#else
    return WSAGetLastError();
#endif
}

/// <summary>
/// 创建UDP套接字, 如果is_server 参数为真, 将会以服务端方式绑定 Endpoint(ip:svc), 否则将会以客户端连接 Endpoint(ip:svc)
/// </summary>
/// <param name="ip">需要 连接/绑定 IP地址</param>
/// <param name="svc">需要 连接/绑定 服务或端口</param>
/// <param name="is_server">是否为服务端, 默认为</param>
/// <returns>成功返回 套接字描述符, 否则返回 INVALID_SOCKET</returns>
SOCKET udp_socket(const char *ip, const char *svc, bool is_server = true);

// -------------------------------------------------------------------------------------- IEvent --------------------------------------------------------------------------------------
class KcpConn;

/// <summary>
/// 网络事件. 每个应用实例都必需实现该事件接口. 
/// 必需实现的接口为 on_message.
/// </summary>
class IEvent : std::enable_shared_from_this<IEvent> {
public: // >>>>>>>>> 类型/符号 >>>>>>>>>
    typedef std::shared_ptr<IEvent> Ptr;

public: // >>>>>>>>> 公共方法 >>>>>>>>>
    /// <summary>
    /// 客户端连接事件, 客户端成功连接后触发.
    /// 该接口可以返回非0, 如果返回非0值, 服务端将丢弃该连接.
    /// </summary>
    /// <param name="conn">已连接KcpConn</param>
    /// <returns>成功返回0, 否则返回-1</returns>
    virtual int on_connected(KcpConn *conn) { return 0; }

    /// <summary>
    /// 
    /// </summary>
    /// <param name="conn"></param>
    virtual void on_disconnected(KcpConn *conn) {}
    virtual void on_error(KcpConn *conn, int err_code) {}
    virtual void on_recv(KcpConn *conn, const char* raw, int raw_len) {}
    virtual void on_send(KcpConn *conn, const char* raw, int raw_len) {}

    virtual int on_message(KcpConn *conn, const char* data, int data_len) = 0;
}; // class

// -------------------------------------------------------------------------------------- KcpConn --------------------------------------------------------------------------------------
//
//
class KcpConn final : std::enable_shared_from_this<KcpConn> {
public: // >>>>>>>>> 类型/符号 >>>>>>>>>
    typedef std::shared_ptr<KcpConn> Ptr;

public: // >>>>>>>>> 公共函数 >>>>>>>>>
    static Ptr connect(IEvent::Ptr event, const char* ip, const char* svc, uint32_t conv = 0, int send_wnd = DEFAULT_SEND_WND, int recv_wnd = DEFAULT_RECV_WND, bool fast_mode = true);

private: // >>>>>>>>> 私有函数 >>>>>>>>>
    static int udp_output(const char* data, int datalen, IKCPCB* kcp, void* ksess);
    static Ptr create(IEvent::Ptr event);

public: // >>>>>>>>> 公共属性 >>>>>>>>>
    bool active() { std::lock_guard<std::mutex> lk(mtx_); return kcp_ == nullptr; }
    SOCKET sockfd() { return ufd_; }

public: // >>>>>>>>> 公共方法 >>>>>>>>>
    ~KcpConn() {
        if (kcp_) {
            ikcp_release(kcp_);
        }
    }

    int update(uint64_t now_ms, int timeout = DEFAULT_TIMEOUT);
    int send(const char* data, int datalen);
    
private: // >>>>>>>>> 私有方法 >>>>>>>>>
    KcpConn(IEvent::Ptr event);
    KcpConn(IEvent::Ptr event, const char* ip, const char* svc, uint32_t conv, int send_wnd, int recv_wnd, bool fast_mode);

    int _recv(SOCKET ufd, const sockaddr* addr, int addrlen, const char* raw, int rawlen, char* data, int datalen);
    void _reset();
    int _set(uint32_t conv, const sockaddr* addr, int addrlen, int send_wnd = DEFAULT_SEND_WND, int recv_wnd = DEFAULT_RECV_WND, bool fast_mode = true);
    void _set_remote(SOCKET ufd, const sockaddr* addr, int addrlen);

    void kcp_create(uint32_t conv, int send_wnd, int recv_wnd, bool fast_mod) { 
        std::lock_guard<std::mutex> lk(mtx_); 
        kcp_ = ::ikcp_create(conv, this);
        assert(kcp_);

        if (fast_mod) { ::ikcp_nodelay(kcp_, 1, 20, 1, 1); }
        else { ::ikcp_nodelay(kcp_, 0, 20, 0, 0); }

        assert(!::ikcp_wndsize(kcp_, send_wnd, recv_wnd) && "ikcp_wndsize called failed");
        assert(!::ikcp_setmtu(kcp_, KCP_MTU) && "ikcp_setmtu called failed");

        kcp_->output = udp_output;
    }

    void kcp_release() { 
        std::lock_guard<std::mutex> lk(mtx_); 
        if (kcp_) {
            ::ikcp_release(kcp_); 
            kcp_ = nullptr; 
        }
        active_time_ = time_ = 0;
    }

    int kcp_input(const char* data, long data_len) { std::lock_guard<std::mutex> lk(mtx_); return kcp_ ? ::ikcp_input(kcp_, data, data_len) : -1; }
    int kcp_recv(char* data, int data_len) { std::lock_guard<std::mutex> lk(mtx_); return kcp_ ? ::ikcp_recv(kcp_, data, data_len) : -1; }
    int kcp_send(const char* data, int data_len) { std::lock_guard<std::mutex> lk(mtx_); return kcp_ ? ::ikcp_send(kcp_, data, data_len) : -1; }
    

private: // >>>>>>>>> 禁用方法 >>>>>>>>>
    KcpConn(const KcpConn&) = delete;
    KcpConn& operator=(const KcpConn&) = delete;

private: // >>>>>>>>> 成员字段 >>>>>>>>>
    SOCKET ufd_;            // UDP套接字
    sockaddr addr_;         // KcpConn对端地址
    int addrlen_;           // KcpConn对端地址长度
    IKCPCB* kcp_;           // KCP
    uint64_t time_;         // 创建时间, 单位毫秒
    uint64_t active_time_;  // 最后激活时间, 单位秒

    std::mutex mtx_;
    IEvent::Ptr event_;

private: // >>>>>>>>> 友元类 >>>>>>>>>
    friend class KcpListener;
}; // class KcpConn;

// -------------------------------------------------------------------------------------- KCP Listener --------------------------------------------------------------------------------------

class KcpListener final {
public: // >>>>>>>>> 类型/符号 >>>>>>>>>
    typedef std::unique_ptr<KcpListener> ptr;
    enum class State {
        Stopped = 0,
        Running,
    };

public: // >>>>>>>>> 公共函数 >>>>>>>>>
    static ptr create(IEvent::Ptr event, const char* ip, const char* port, int nthread = 0);

private: // >>>>>>>>> 私有函数 >>>>>>>>>
public: // >>>>>>>>> 公共属性 >>>>>>>>>
public: // >>>>>>>>> 公共方法 >>>>>>>>>
    ~KcpListener() { stop(); }
    void run();
    void stop() { state_ = State::Stopped; }

private: // >>>>>>>>> 私有方法 >>>>>>>>>
    KcpListener(IEvent::Ptr event, const char* ip, const char* port, int nthread, int max_conn);
    void work_thread(const char* ip, const char* port);
    void update_thread();

private: // >>>>>>>>> 禁用方法 >>>>>>>>>
private: // >>>>>>>>> 成员字段 >>>>>>>>>
    State state_;
    std::vector<std::thread> thread_pool_;
    std::unordered_map<uint32_t, KcpConn::Ptr> sess_map_;

private: // >>>>>>>>> 友元类 >>>>>>>>>
}; // class KcpListener;

} // namespace net
} // namespace xq

#endif // __NET_HPP__
