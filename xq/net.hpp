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

// ---------------------------------------------------------------------------- system ----------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------- C ----------------------------------------------------------------------------
#include <assert.h>
#include <errno.h>
#include <memory.h>

// ---------------------------------------------------------------------------- C++ ----------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// ----------------------------------------------------------------------------- 3rd party -----------------------------------------------------------------------------
#include "ikcp.h"

// ----------------------------------------------------------------------------- xq -----------------------------------------------------------------------------
#include "tools.hpp"

namespace xq {
namespace net {

// ----------------------------------------------------------------------------- 类型/符号 -----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------- 常量 -----------------------------------------------------------------------------
constexpr int DEFAULT_SEND_WND = 512;   // 发送窗口大小
constexpr int DEFAULT_RECV_WND = 512;   // 接收窗口大小
constexpr int DEFAULT_TIMEOUT  = 60;    // 默认超时(秒)

// 以太网默认MTU - 以太网头 - IPV4头(20~60)/IPV6(40+) - UDP头
constexpr int KCP_MTU = 1500 - 14 - 60 - 8;                     // KCP MTU
constexpr int KCP_HEAD_SIZE = 24;                               // KCP消息头长度
constexpr int MAX_DATA_SIZE = 128 * (KCP_MTU - KCP_HEAD_SIZE);  // 消息包最大长度: 181(kB)

// ----------------------------------------------------------------------------- 错误码 -----------------------------------------------------------------------------
constexpr int ERR_KCP_INVALID = -100; // 无效的KCP
constexpr int ERR_KCP_TIMEOUT = -101; // 超时

// ----------------------------------------------------------------------------- 公共函数 -----------------------------------------------------------------------------

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
/// <param name="local">本地Endpoint</param>
/// <param name="remote">远端Endpoint</param>
/// <returns>成功返回 套接字描述符, 否则返回 INVALID_SOCKET</returns>
SOCKET udp_socket(const char* local, const char* remote, sockaddr *addr = nullptr, socklen_t *addrlen = nullptr);

// ----------------------------------------------------------------------------- IEvent -----------------------------------------------------------------------------
class KcpConn;

/// <summary>
/// 网络事件. 每个应用实例都必需实现该事件接口. 
/// 必需实现的接口为 on_message.
/// </summary>
class IEvent {
public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 类型/符号 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    typedef std::shared_ptr<IEvent> Ptr;

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共方法 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    IEvent() {}
    IEvent(const IEvent&) = delete;
    IEvent& operator=(const IEvent&) = delete;

    /// <summary>
    /// 客户端连接事件, 客户端成功连接后触发.
    /// 该接口可以返回非0, 如果返回非0值, 底层将其丢弃该连接.
    /// </summary>
    /// <param name="conn">已连接KcpConn</param>
    /// <returns>成功返回0, 否则返回-1</returns>
    virtual int on_connected(KcpConn */*conn*/) { return 0; }

    /// <summary>
    /// 客户端连接断开事件, 客户端连接断开后触发.
    /// TODO: 由于是UDP, 所以没有真正意义上的连接, 所谓的断开, 只是客户端读超时.
    /// </summary>
    /// <param name="conn">已断开连接的KcpConn</param>
    virtual void on_disconnected(KcpConn */*conn*/) {}

    /// <summary>
    /// 客户端错误事件, 当客户端出现错误时触发.
    /// TODO: 目前并没有任务地方调用该事件
    /// </summary>
    /// <param name="conn">出现错误的KcpConn</param>
    /// <param name="err_code">错误码</param>
    virtual void on_error(KcpConn */*conn*/, int /*err_code*/) {}

    /// <summary>
    /// 原始码流接收事件
    /// 原始码流会包含KCP协议头, 除此以外, 原始码流可能一次接收到多个KCP包.
    /// 原始码流也可能只是KCP底层消息, 例如 KCP-ACK
    /// </summary>
    /// <param name="conn">KcpConn</param>
    /// <param name="raw">原始码流</param>
    /// <param name="raw_len">原始码流长度</param>
    virtual void on_recv(KcpConn */*conn*/, const char* /*raw*/, size_t /*raw_len*/) {}

    /// <summary>
    /// 原始码流发送事件
    /// 原始码流会包含KCP协议头, 除此以外, 原始码流可能会一次发送多个KCP包.
    /// 原始码流也可能只是KCP底层消息, 例如 KCP-ACK
    /// </summary>
    /// <param name="conn">KcpConn</param>
    /// <param name="raw">原始码流</param>
    /// <param name="raw_len">原始码流长度</param>
    virtual void on_send(KcpConn */*conn*/, const char* /*raw*/, size_t /*raw_len*/) {}

    /// <summary>
    /// 消息事件. 当该事件返回非0时, 底层会断开重置KcpConn的连接.
    /// </summary>
    /// <param name="conn">KcpConn</param>
    /// <param name="data">消息数据</param>
    /// <param name="data_len">消息长度</param>
    /// <returns>成功返回0, 否则返回-1</returns>
    virtual int on_message(KcpConn */*conn*/, const char* /*data*/, size_t /*data_len*/) = 0;
}; // class IEvent

// ----------------------------------------------------------------------------- KcpConn -----------------------------------------------------------------------------

/// <summary>
/// Kcp客户端
/// </summary>
class KcpConn final : std::enable_shared_from_this<KcpConn> {

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 类型/符号 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    typedef std::shared_ptr<KcpConn> Ptr;

public: // >>>>>>>>> 公共函数 >>>>>>>>>

    /// <summary>
    /// 连接KCP服务器
    /// </summary>
    /// <param name="event">IEvent 实现</param>
    /// <param name="local">本端地址</param>
    /// <param name="remote">服务端地址</param>
    /// <param name="conv">客户conv, conv必需为非0值. 底层不会去自动生成conv, 该值必需由开发者自行生成. 该值用于同Kcp服务端进行交互, 必需确保该值在服务端的唯一性</param>
    /// <param name="send_wnd">发送窗口, 默认为256</param>
    /// <param name="recv_wnd">接收窗口, 默认为256</param>
    /// <param name="fast_mode">是否为极速模式, 极速模式会增加网络压力</param>
    /// <returns></returns>
    static Ptr connect(IEvent::Ptr event, const char* local, const char* remote, uint32_t conv) {
        assert(conv);
        return Ptr(new KcpConn(event, local, remote, conv));
    }

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 私有函数 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    /// <summary>
    /// 创建默认KcpConn, 该函数由KcpListener调用.
    /// </summary>
    /// <param name="event">IEvent 实现</param>
    /// <returns>成功返回默认的KcpConn实例, 否则返回nullptr</returns>
    static Ptr create(IEvent::Ptr event, int timeout) {
        return Ptr(new KcpConn(event, timeout));
    }

    /// <summary>
    /// UDP发送函数, 用于KCP回调.
    /// </summary>
    /// <param name="data">将要发送的数据, 原始码流</param>
    /// <param name="data_len">原始码流长度</param>
    /// <param name="kcp">客户端KCP实例</param>
    /// <param name="conn">KcpConn</param>
    /// <returns>成功返回0, 否则返回-1</returns>
    static int udp_output(const char* data, int data_len, IKCPCB* kcp, void* conn);

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共属性 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    /// <summary>
    /// 判断当前KcpConn是否激活
    /// </summary>
    /// <returns>激活返回true, 否则返回false</returns>
    bool active() { 
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);
        return kcp_ != nullptr; 
    }

    /// <summary>
    /// 获取KcpConn conv值.
    /// </summary>
    /// <returns>激活时返顺conv, 否则返回0</returns>
    uint32_t conv() {
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);
        return kcp_ ? kcp_->conv : 0; 
    }

    /// <summary>
    /// 获取底层UDP 描述符
    /// </summary>
    /// <returns>激活时返回UDP描述符, 否则返回 SOCKET_INVALID</returns>
    SOCKET sockfd() const { return ufd_; }

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共方法 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    ~KcpConn() { 
        if (kcp_) 
            ::ikcp_release(kcp_);
        // TODO: 当为客户端时, 关闭sockfd
    }

    int recv();

    /// <summary>
    /// 发送消息
    /// </summary>
    /// <param name="data">消息数据</param>
    /// <param name="data_len">数据长度</param>
    /// <returns>
    ///     0: 成功; 
    ///     -100: KcpConn 已失效; 
    ///     others: 错误;
    /// </returns>
    int send(const char* data, int data_len) {
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);
        return kcp_ ? ::ikcp_send(kcp_, data, data_len) : ERR_KCP_INVALID;
    }

    void set(int send_wnd = DEFAULT_SEND_WND, int recv_wnd = DEFAULT_RECV_WND, bool fast_mode = true, int timeout = DEFAULT_TIMEOUT) {
        assert(kcp_ && "ikcp is not init yet");
        assert(timeout >= 0);

        if (fast_mode)
            ::ikcp_nodelay(kcp_, 1, 20, 1, 1);
        else
            ::ikcp_nodelay(kcp_, 0, 20, 0, 0);

        assert(!::ikcp_wndsize(kcp_, send_wnd, recv_wnd) && "ikcp_wndsize called failed");
        assert(!::ikcp_setmtu(kcp_, KCP_MTU) && "ikcp_setmtu called failed");
        
        if (timeout)
            timeout_ = timeout;
    }
    
    /// <summary>
    /// KCP UPDATE, 用于处理KCP协议消息.
    /// 当该方法返回非0时, 表示当前KcpConn已处于无效状态.
    /// </summary>
    /// <param name="now_ms">当前时间毫秒级时间戳</param>
    /// <param name="timeout">超时值</param>
    /// <returns>
    ///     0: 成功;
    ///     -101: KcpConn 超时;
    /// </returns>
    int update(uint64_t now_ms) {
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);

        // 如果当前KcpConn未激活, 不作检查.
        if (!kcp_)
            return 0;

        if (timeout_ > 0 && now_ms / 1000 - active_time_ > (uint64_t)timeout_)
            return ERR_KCP_TIMEOUT;

        ::ikcp_update(kcp_, (uint32_t)(now_ms - time_));
        return 0;
    }
    
private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 私有方法 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    /// <summary>
    /// 将原始码流解析为消息数据
    /// </summary>
    /// <param name="ufd">产生数据的UDP套接字</param>
    /// <param name="addr">产生数据的对端地址</param>
    /// <param name="addrlen">地址长度</param>
    /// <param name="raw">原始码流</param>
    /// <param name="raw_len">原始码流长度</param>
    /// <param name="data">消息数据缓冲区</param>
    /// <param name="data_len">缓冲区大小</param>
    /// <returns>成功返回0, 否则返加错误.</returns>
    int _recv(SOCKET ufd, uint32_t conv, const sockaddr* addr, int addrlen, const char* raw, int raw_len, char* data, int data_len);

    /// <summary>
    /// 重置KcpConn为未激活状态, 由服务端调用
    /// </summary>
    void _reset() {
        event_->on_disconnected(this);
        std::lock_guard<xq::tools::SpinMutex> lk(mtx_);

        if (ufd_ != INVALID_SOCKET) {
            ufd_ = INVALID_SOCKET;
        }

        if (kcp_) {
            ::ikcp_release(kcp_);
            kcp_ = nullptr;
        }

        time_ = active_time_ = 0;
    }

    /// <summary>
    /// 设置对端地址.
    /// 当服务端需要发送消息给客户端时, 需要确定用来发送的 udp sockfd 和对端的地址.
    /// </summary>
    /// <param name="ufd">udp sockfd</param>
    /// <param name="addr">需要发送的地址</param>
    /// <param name="addrlen">发送地址上长</param>
    void _set_remote(SOCKET ufd, const sockaddr* addr, int addrlen) {
        if (ufd_ != ufd || ufd_ == INVALID_SOCKET) {
            ufd_ = ufd;
        }

        if (!::memcmp(&addr_, addr, addrlen)) {
            ::memcpy(&addr_, addr, addrlen);
            addrlen_ = addrlen;
        }
    }

    /// <summary>
    /// 构造函数: 创建默认KcpConn.由服务端调用
    /// </summary>
    /// <param name="event">IEvent 实现</param>
    KcpConn(IEvent::Ptr event, int timeout) :
        ufd_(INVALID_SOCKET),
        addrlen_(0),
        kcp_(nullptr),
        timeout_(timeout),
        time_(0),
        active_time_(0),
        event_(event) {
        assert(timeout_ >= 0);
        ::memset(&addr_, 0, sizeof(addr_));
    }

    /// <summary>
    /// 构造函数: 创建已激活的KcpConn, 由客户端调用.
    /// </summary>
    /// <param name="event">IEvent 实现</param>
    /// <param name="local">本地监听</param>
    /// <param name="remote">远端地址</param>
    /// <param name="conv">kcp conv</param>
    /// <param name="send_wnd">发送窗口大小</param>
    /// <param name="recv_wnd">接收窗口大小</param>
    /// <param name="fast_mode">是否为极速模式</param>
    KcpConn(IEvent::Ptr event, const char* local, const char* remote, uint32_t conv);

    KcpConn(const KcpConn&) = delete;
    KcpConn& operator=(const KcpConn&) = delete;

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 成员字段 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    SOCKET ufd_;            // UDP套接字
    sockaddr addr_;         // KcpConn对端地址
    socklen_t addrlen_;           // KcpConn对端地址长度
    IKCPCB* kcp_;           // KCP
    int timeout_;           // 超时
    uint64_t time_;         // 创建时间, 单位毫秒
    uint64_t active_time_;  // 最后激活时间, 单位秒

    xq::tools::SpinMutex mtx_;
    IEvent::Ptr event_;

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 友元类 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    friend class KcpListener;
}; // class KcpConn;

// ----------------------------------------------------------------------------- KCP Listener -----------------------------------------------------------------------------

/// <summary>
/// 
/// </summary>
class KcpListener final {

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 类型/符号 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    typedef std::unique_ptr<KcpListener> ptr;

    /// <summary>
    /// KcpListener 状态
    /// </summary>
    enum class State {
        Stopped = 0, // 停止状态
        Stopping,    // 正在停止状态
        Running,     // 运行状态
    };

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共函数 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    /// <summary>
    /// 创建KcpListener
    /// </summary>
    /// <param name="timeout">读超时(秒), 默认60秒</param>
    /// <returns>KcpListener 实例</returns>
    static ptr create(int timeout = DEFAULT_TIMEOUT) { return ptr(new KcpListener(timeout)); }

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 私有函数 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共属性 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

public: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 公共方法 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    ~KcpListener() { stop(); }

    /// <summary>
    /// 启动服务
    /// 当 nthread 参数为 0时, 线程池大小会根据当前设置的CPU核心数自动设置.
    /// </summary>
    /// <param name="event">IEvent 实现</param>
    /// <param name="host">监听地址</param>
    /// <param name="nthread">线程池大小, 默认为0.</param>
    /// <param name="max_conn">最大连接数</param>
    void run(IEvent::Ptr event, const char* host, uint32_t nthread, uint32_t max_conn);

    /// <summary>
    /// 停止服务
    /// </summary>
    void stop() { state_ = State::Stopping; }

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 私有方法 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    KcpListener(int timeout)  :
        timeout_(timeout),
        state_(State::Stopped),
        ncur_(0) {
        assert(timeout_ > 0);
    }

    /// <summary>
    /// 工作线程
    /// </summary>
    /// <param name="host">监听地址</param>
    void work_thread(const char* host);

    /// <summary>
    /// Kcp协议工作线程
    /// </summary>
    void update_thread();

    KcpListener() = delete;
    KcpListener(const KcpListener&) = delete;
    KcpListener& operator=(const KcpListener&) = delete;

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 成员字段 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    int timeout_;
    State state_;

    std::atomic<uint32_t> ncur_;
    std::thread update_thr_;
    std::vector<std::thread> thread_pool_;
    std::vector<KcpConn::Ptr> conn_map_;

private: // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 友元类 >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}; // class KcpListener;

} // namespace net
} // namespace xq

#endif // __NET_HPP__
