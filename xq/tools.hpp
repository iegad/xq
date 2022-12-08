#ifndef __TOOLS_HPP__
#define __TOOLS_HPP__

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif // !_WIN32

#include <stdint.h>

#include <atomic>
#include <chrono>
#include <string>

namespace xq {
namespace tools {

/// <summary>
/// 获取当前时间戳(毫秒)
/// </summary>
/// <returns>当前时间戳(毫秒)</returns>
inline uint64_t get_time_ms() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

#ifndef _WIN32
inline uint64_t htonll(uint64_t v) {
    return ((uint64_t)(htonl(v)) << 32) + htonl(v >> 32);
}
#endif

inline bool is_le() {
	static constexpr union { uint16_t a; uint8_t b; } tt = { 0x0001 };
	return tt.b == 0x01;
}

inline bool is_be() {
	return !is_le();
}

inline uint16_t to_le(uint16_t v) {
       return is_le() ? v : ::htons(v);
}

inline uint16_t to_be(uint16_t v) {
       return is_le() ? ::htons(v) : v;
}

inline uint32_t to_le(uint32_t v) {
       return is_le() ? v : ::htonl(v);
}

inline uint32_t to_be(uint32_t v) {
       return is_be() ? ::htonl(v) : v;
}

inline uint64_t to_le(uint64_t v) {
       return is_le() ? v : htonll(v);
}

inline uint64_t to_be(uint64_t v) {
       return is_be() ? htonll(v) : v;
}

std::string bin2hex(const uint8_t* data, size_t data_len);

class SpinMutex final {
public:
    SpinMutex() {}

    void lock();

    bool try_lock() {
        return !mtx_.load(std::memory_order_relaxed) && !mtx_.exchange(true, std::memory_order_acquire);
    }

    void unlock() {
        mtx_.store(false, std::memory_order_release);
    }

private:
    SpinMutex(const SpinMutex&) = delete;
    SpinMutex& operator=(const SpinMutex&) = delete;

    std::atomic<bool> mtx_;
}; // class SpinMutex;

} // namespace tools
} // namespace xq

#endif // __TOOLS_HPP__
