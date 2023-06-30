#ifndef __XQ_NET_RUX_SERVER__
#define __XQ_NET_RUX_SERVER__


#include <list>
#include <unordered_set>
#include "xq/net/rux.hpp"


namespace xq {
namespace net {


template <class TService>
class RuxServer {
public:
    typedef RuxServer* ptr;
    typedef Udx<RuxServer<TService>> UDX;


    explicit RuxServer(TService* service)
        : nprocessor_(sys_cpus() - 1) 
        , service_(service)
    {}


    void on_run(UDX* udx) {
        running_ = true;

        for (int i = 1; i <= RUX_RID_MAX; i++) {
            sessions_.emplace_back(new Rux(i, 0, udx->snd_que()));
        }

        if (nprocessor_ == 0) {
            nprocessor_ = 1;
        }

        update_thread_ = std::thread(std::bind(&RuxServer::_update_thread, this));
        for (int i = 0; i < nprocessor_; i++) {
            pfm_ques_.emplace_back(new FrameQueue(FRAME_QUE_SIZE));
            rux_thread_pool_.emplace_back(std::thread(std::bind(&RuxServer::_rux_thread, this, pfm_ques_[i])));
        }
    }


    ~RuxServer() {
        for (auto s : sessions_) {
            delete s;
        }

        for (auto q : pfm_ques_) {
            delete q;
        }
    }


    TService& service() {
        return service_;
    }


    void on_stop(UDX* udx) {
        running_ = false;

        while (rux_thread_pool_.size()) {
            auto itr = rux_thread_pool_.begin();
            if (itr->joinable()) {
                itr->join();
            }
            rux_thread_pool_.erase(itr);
        }

        if (update_thread_.joinable()) {
            update_thread_.join();
        }

        Frame::ptr pfms[128];
        int n, i;

        for (auto q : pfm_ques_) {
            do {
                n = q->try_dequeue_bulk(pfms, 128);
                for (i = 0; i < n; i++) {
                    delete pfms[i];
                }
            } while (n > 0);
        }

        udx->clear_snd_que();
    }


#if defined(__linux__) && !defined(__ANDROID__)


    void on_recv(UDX*, Frame::ptr *pfms, int n) {
        typedef std::vector<Frame::ptr> QueItem;
        typedef std::vector<QueItem> Ques;

        static int round_id = 0;
        static Ques q_pfms;

        if (!q_pfms.size()) {
            q_pfms.resize(nprocessor_);
        }

        uint32_t rid = 0;
        Frame::ptr pfm;

        for (int i = 0; i < n; i++) {
            pfm = pfms[i];
            if (pfm->len > UDP_MTU || pfm->len < RUX_FRM_EX_SIZE) {
                continue;
            }

            u24_decode(pfm->raw, &rid);
            if (rid == 0 || rid > RUX_RID_MAX) {
                return;
            }

            Rux::ptr rux = sessions_[rid - 1];
            if (rux->state() < 0) {
                rux->set_state(1);
                rux->set_qid(round_id++);
                if (round_id == nprocessor_) {
                    round_id = 0;
                }
                sess_lkr_.lock();
                active_session_.insert(rid);
                sess_lkr_.unlock();
                service_->on_connected(rux);
            }

            pfm->ex = rux;
            q_pfms[rux->qid()].emplace_back(pfm);
        }

        for (size_t qid = 0; qid < q_pfms.size(); qid++) {
            QueItem &qi = q_pfms[qid];
            if (!pfm_ques_[qid]->enqueue_bulk(&qi[0], qi.size())) {
                DLOG("enter que failed\n");
            }
            qi.clear();
        }
    }


#else


    void on_recv(UDX* udx, int err, Frame::ptr pfm) {
        static int round_id = 0;

        if (pfm->len < RUX_FRM_EX_SIZE) {
            return;
        }

        uint32_t rid = 0;

        u24_decode(pfm->raw, &rid);
        if (rid == 0 || rid > RUX_RID_MAX) {
            return;
        }

        Rux::ptr rux = sessions_[rid - 1];
        if (rux->state() < 0) {
            rux->set_state(1);
            rux->set_qid(round_id++);
            if (round_id == nprocessor_) {
                round_id = 0;
            }
            sess_lkr_.lock();
            active_session_.insert(rid);
            sess_lkr_.unlock();
            service_->on_connected(rux);
        }

        pfm->ex = rux;
        pfm_ques_[rux->qid()]->try_enqueue(pfm);
    }


#endif


    void on_send(UDX*, int err, Frame::ptr) {
        if (err) {
            // TODO:
        }
    }


private:
    void _rux_thread(FrameQueue* que) {
        constexpr int FRAME_SIZE = 128;
        constexpr int TIMEOUT    = 200 * 1000;

        Frame::ptr pfms[FRAME_SIZE];
        Frame::ptr pfm;
        size_t npfms, i, nmsg;
        uint8_t* msg = new uint8_t[RUX_MSG_MAX];
        Rux::ptr rux;

        while (running_) {
            npfms = que->wait_dequeue_bulk_timed(pfms, FRAME_SIZE, TIMEOUT);
            for (i = 0; i < npfms; i++) {
                pfm = pfms[i];
                rux = (Rux::ptr)pfm->ex;
                ASSERT(rux);
                if (rux->input(pfm) == 0) {
                    do {
                        nmsg = rux->recv(msg);
                        if (nmsg > 0) {
                            service_->on_message(rux, msg, nmsg);
                        }
                    } while (nmsg > 0);
                }

                delete pfm;
            }
            std::this_thread::yield();
        }

        delete[] msg;
    }


    void _update_thread() {
#ifndef _WIN32
        timeval timeout = { 0, 0 };
#endif
        uint64_t now_us;
        size_t n;
        Rux* rux;
        std::unordered_set<uint32_t>::iterator itr;

        while (running_) {
            now_us = sys_time();
            sess_lkr_.lock();
            n = active_session_.size();
            sess_lkr_.unlock();

            sess_lkr_.lock();
            itr = active_session_.begin();
            sess_lkr_.unlock();

            while (n > 0) {
                rux = sessions_[*itr - 1];
                if (rux->update(now_us) < 0) {
                    sess_lkr_.lock();
                    active_session_.erase(itr++);
                    sess_lkr_.unlock();
                    service_->on_disconnected(rux);
                }
                else {
                    itr++;
                }

                n--;
            }

#ifdef _WIN32
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
#else
            timeout.tv_usec = 20000;
            ::select(0, nullptr, nullptr, nullptr, &timeout);
#endif
        }
    }


    bool      running_ = false;
    int       nprocessor_;
    TService* service_;

    SpinLock                     sess_lkr_;
    std::vector<Rux*>            sessions_;
    std::unordered_set<uint32_t> active_session_;

    std::thread              update_thread_;
    std::list<std::thread>   rux_thread_pool_;   // rux 线程
    std::vector<FrameQueue*> pfm_ques_;


    RuxServer(const RuxServer&) = delete;
    RuxServer(const RuxServer&&) = delete;
    RuxServer& operator=(const RuxServer&) = delete;
    RuxServer& operator=(const RuxServer&&) = delete;
}; // class RuxServer;


} // namespace net;
} // namespace xq;


#endif // !__XQ_NET_RUX_SERVER__
