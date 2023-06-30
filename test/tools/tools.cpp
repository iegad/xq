#include "xq/net/rux.hpp"


void
basic_test() {
    DLOG("TEST BASIC\n");
    uint8_t buf[1000]{};

    uint8_t u8 = 0x11;
    ASSERT(xq::net::u8_encode(u8, buf));
    uint8_t _u8 = 0;
    ASSERT(xq::net::u8_decode(buf, &_u8));
    ASSERT(u8 == _u8 && "u8_[en|de]code failed");

    int8_t s8 = 0x8F;
    ASSERT(xq::net::s8_encode(s8, buf));
    int8_t _s8 = 0;
    ASSERT(xq::net::s8_decode(buf, &_s8));
    ASSERT(s8 == _s8 && "u8_[en|de]code failed");

    uint16_t u16 = 0x1234;
    ASSERT(xq::net::u16_encode(u16, buf));
    uint16_t _u16 = 0;
    ASSERT(xq::net::u16_decode(buf, &_u16));
    ASSERT(u16 == _u16 && "u16_[en|de]code failed");

    int16_t s16 = 0x1234;
    ASSERT(xq::net::s16_encode(s16, buf));
    int16_t _s16 = 0;
    ASSERT(xq::net::s16_decode(buf, &_s16));
    ASSERT(s16 == _s16 && "s16_[en|de]code failed");

    uint32_t u24 = 0x123456;
    ASSERT(xq::net::u24_encode(u24, buf));
    uint32_t _u24 = 0;
    ASSERT(xq::net::u24_decode(buf, &_u24));
    ASSERT(u24 == _u24 && "u24_[en|de]code failed");

    int32_t s24 = 0x123456;
    ASSERT(xq::net::s24_encode(s24, buf));
    int32_t _s24 = 0;
    ASSERT(xq::net::s24_decode(buf, &_s24));
    ASSERT(s24 == _s24 && "s24_[en|de]code failed");

    uint32_t u32 = 0x12345678;
    ASSERT(xq::net::u32_encode(u32, buf));
    uint32_t _u32 = 0;
    ASSERT(xq::net::u32_decode(buf, &_u32));
    ASSERT(u32 == _u32 && "u32_[en|de]code failed");

    int32_t s32 = 0x123456;
    ASSERT(xq::net::s32_encode(s32, buf));
    int32_t _s32 = 0;
    ASSERT(xq::net::s32_decode(buf, &_s32));
    ASSERT(s32 == _s32 && "s32_[en|de]code failed");

    uint64_t u48 = 0x123456789ABC;
    ASSERT(xq::net::u48_encode(u48, buf));
    uint64_t _u48 = 0;
    ASSERT(xq::net::u48_decode(buf, &_u48));
    ASSERT(u48 == _u48 && "u48_[en|de]code failed");

    int64_t s48 = 0x123456789ABC;
    ASSERT(xq::net::s48_encode(s48, buf));
    int64_t _s48 = 0;
    ASSERT(xq::net::s48_decode(buf, &_s48));
    ASSERT(s48 == _s48 && "s48_[en|de]code failed");

    uint64_t u64 = 0x123456789ABCEFFF;
    ASSERT(xq::net::u64_encode(u64, buf));
    uint64_t _u64 = 0;
    ASSERT(xq::net::u64_decode(buf, &_u64));
    ASSERT(u64 == _u64 && "u64_[en|de]code failed");

    int64_t s64 = 0x123456789ABCEFFF;
    ASSERT(xq::net::s64_encode(s64, buf));
    int64_t _s64 = 0;
    ASSERT(xq::net::s64_decode(buf, &_s64));
    ASSERT(s64 == _s64 && "s64_[en|de]code failed");

    DLOG("TEST BASIC SUCCESSFUL...!!!\n");
}


class UdxEvent {
public:
    typedef xq::net::Udx<UdxEvent> UDX;
    using Frame = xq::net::Frame;

    void on_recv(UDX*, int err, Frame::ptr pfm) {

    }

    void on_send(UDX*, int err, Frame::ptr pfm) {

    }

    void on_run(UDX*) {}

    void on_stop(UDX*) {}
};


void
rux_client() {
    using Segment = xq::net::Rux::Segment;
    using Frame = xq::net::Frame;

    sockaddr_storage addr{};
    socklen_t addrlen = sizeof(addr);
    ASSERT(!str2addr("192.168.0.101:6688", &addr, &addrlen));

    UdxEvent ev;
    UdxEvent::UDX::ptr pUdx = new UdxEvent::UDX(":8888", &ev);
    pUdx->run();

    std::vector<Segment::ptr> segs;
    for (size_t i = 0; i < 10; i++) {
        uint8_t pdata[10]{ i };

        Segment::ptr seg = new Segment;
        seg->cmd = xq::net::RUX_CMD_CON;
        seg->sn = i;
        seg->us = i + 1;
        seg->len = sizeof(pdata);
        seg->frg = 10 - i - 1;
        ::memcpy(seg->data, pdata, sizeof(pdata));

        segs.emplace_back(seg);
    }

    std::vector<Frame::ptr> pfms;
    for (size_t i = 0; i < 10; i++) {
        Frame::ptr pfm = new Frame(&addr, addrlen);
        uint8_t* p = pfm->raw;
        pfm->len = segs[i]->encode(p, sizeof(pfm->raw));
    }

    ASSERT(pUdx->send(pfms[0]) > 0);
    ASSERT(pUdx->send(pfms[9]) > 0);
    ASSERT(pUdx->send(pfms[8]) > 0);
    ASSERT(pUdx->send(pfms[6]) > 0);
    ASSERT(pUdx->send(pfms[7]) > 0);
    ASSERT(pUdx->send(pfms[1]) > 0);
    ASSERT(pUdx->send(pfms[5]) > 0);
    ASSERT(pUdx->send(pfms[2]) > 0);
    ASSERT(pUdx->send(pfms[3]) > 0);
    ASSERT(pUdx->send(pfms[4]) > 0);

    pUdx->wait();
    delete pUdx;
}


int
main(int, char**) {
    basic_test();
    rux_client();
}