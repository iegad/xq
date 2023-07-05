#ifndef __XQ_NET_RUX_CLIENT__
#define __XQ_NET_RUX_CLIENT__


#include "xq/net/rux.hpp"


namespace xq {
namespace net {


template <class TService>
class RuxClient {
public:
    typedef RuxClient* ptr;
    typedef Udx<RuxClient<TService>> UDX;


    explicit RuxClient(uint32_t rid, TService *service)
        : running_(false)
        , rid_(rid)
        , service_(service) {
        ASSERT(rid > 0 && rid <= RUX_RID_MAX);
    }


    ~RuxClient() {
        for (auto& itr : node_map_) {
            delete itr.second;
        }
    }


    void on_send(UDX*, int, Frame::ptr) {

    }


#if defined(__linux__) && !defined(__ANDROID__)
    

    void on_recv(UDX*, Frame::ptr* pfms, int n) {
        static char endpoint[ENDPOINT_STR_LEN] = {0};
        static uint8_t* msg = new uint8_t[RUX_MSG_MAX];

        Frame::ptr pfm;

        for (int i = 0; i < n; i++) {
            pfm = pfms[i];
            if (pfm->len <= UDP_MTU) {
                do {
                    if (addr2str(&pfm->name, endpoint, ENDPOINT_STR_LEN)) {
                        break;
                    }

                    auto itr = node_map_.find(endpoint);
                    if (itr == node_map_.end()) {
                        break;
                    }

                    Rux::ptr rux = itr->second;
                    pfm->ex = rux;
                    if (rux->input(pfm)) {
                        break;
                    }

                    int msglen;
                    do {
                        msglen = rux->recv(msg);
                        if (msglen > 0) {
                            service_->on_message(rux, msg, msglen);
                        }
                    } while (msglen > 0);
                } while (0);
            }

            delete pfm;
        }
    }


#else


    void on_recv(UDX* udx, int err, Frame::ptr pfm) {
        static char endpoint[ENDPOINT_STR_LEN] = {0};
        static uint8_t* msg = new uint8_t[RUX_MSG_MAX];

        do {
            if (err) {
                // TODO: error
                break;
            }

            if (addr2str(&pfm->name, endpoint, ENDPOINT_STR_LEN)) {
                break;
            }

            auto itr = node_map_.find(endpoint);
            if (itr == node_map_.end()) {
                break;
            }

            Rux::ptr rux = itr->second;
            pfm->ex = rux;
            if (rux->input(pfm)) {
                break;
            }

            int msglen;
            do {
                msglen = rux->recv(msg);
                if (msglen > 0) {
                    service_->on_message(rux, msg, msglen);
                }
            } while (msglen > 0);
        } while (0);

        delete pfm;
    }


#endif


    void on_run(UDX*) {
        running_ = true;
        update_thread_ = std::thread(std::bind(&RuxClient::_update_thread, this));
    }


    void on_stop(UDX* udx) {
        running_ = false;
        if (update_thread_.joinable()) {
            update_thread_.join();
        }

        udx->clear_snd_que();
    }


    uint64_t xmit() const {
        uint64_t xmit = 0;
        for (auto& n : node_map_) {
            xmit += n.second->xmit();
        }
        return xmit;
    }


    inline void connect_node(const std::string &endpoint, uint64_t now_us, FrameQueue &snd_que) {
        Rux* rux = new Rux(rid_, now_us, snd_que);
        sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        ::memset(&addr, 0, addrlen);
        std::string ip = endpoint.substr(0, endpoint.rfind(':'));
        if (ip.empty()) {
            ip = "0.0.0.0";
        }
        addr.ss_family = check_ip_family(ip);
        ASSERT(!str2addr(endpoint.c_str(), &addr, &addrlen));
        rux->set_rmt_addr(&addr, addrlen);
        rux->set_state(0);
        node_map_.insert(std::make_pair(endpoint, rux));
    }


    int send(const char* endpoint, const uint8_t* msg, uint16_t msglen) {
        auto itr = node_map_.find(endpoint);
        if (itr == node_map_.end()) {
            return -1;
        }

        return itr->second->send(msg, msglen);
    }


private:
    void _update_thread() {
        std::unordered_map<std::string, Rux*>::iterator itr;
        Rux* rux;
        uint64_t now_us;
#ifndef _WIN32
        timeval timeout = {0, 0};
#endif

        while (running_) {
            now_us = sys_time();

            itr = node_map_.begin();
            while (itr != node_map_.end()) {
                rux = itr->second;
                if (rux->update(now_us) < 0) {
                    rux->set_state(1);
                }
                itr++;
            }
#ifdef _WIN32
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
#else
            timeout.tv_usec = 15000;
            ::select(0, nullptr, nullptr, nullptr, &timeout);
#endif
        }
    }


    bool        running_;
    uint32_t    rid_;
    TService*   service_;
    std::thread update_thread_;    // update 线程

    std::unordered_map<std::string, Rux::ptr> node_map_;      // service node's map


    RuxClient(const RuxClient&) = delete;
    RuxClient(const RuxClient&&) = delete;
    RuxClient& operator=(const RuxClient&) = delete;
    RuxClient& operator=(const RuxClient&&) = delete;
}; // class RuxClient;


} // namespace net
} // namespace xq


#endif // __XQ_NET_RUX_CLIENT__
