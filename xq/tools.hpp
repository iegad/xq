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
/// 获取当前时间戳(秒)
/// </summary>
/// <returns></returns>
inline int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// <summary>
/// 获取当前时间戳(毫秒)
/// </summary>
/// <returns></returns>
inline int64_t now_milli() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// <summary>
/// 获取当前时间戳(微秒)
/// </summary>
/// <returns></returns>
inline int64_t now_micro() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// <summary>
/// 获取当前时间戳(纳秒)
/// </summary>
/// <returns></returns>
inline int64_t now_nano() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// <summary>
/// uint16_t 反转字节序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint16_t __rvs_u16(uint16_t v) {
    return (v << 8) | (v >> 8);
}

/// <summary>
/// uint32_t 反转字节序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint32_t __rvs_u32(uint32_t v) {
    return (((uint32_t)(__rvs_u16(v)) << 16)) | (__rvs_u16(v >> 16));
}

/// <summary>
/// uint64_t 反转字节序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint64_t __rvs_u64(uint64_t v) {
    return (((uint64_t)(__rvs_u32(v)) << 32)) | (__rvs_u32(v >> 32));
}

/// <summary>
/// 当前机器字节序是否为小端序
/// </summary>
/// <returns></returns>
inline bool is_le() {
	static constexpr union { uint16_t a; uint8_t b; } tt = { 0x0001 };
	return tt.b == 0x01;
}

/// <summary>
/// 当前机器字节序是否为大端序
/// </summary>
/// <returns></returns>
inline bool is_be() {
	return !is_le();
}

/// <summary>
/// 另 v 为小端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint16_t to_le_u16(uint16_t v) {
   return is_le() ? v : __rvs_u16(v);
}

/// <summary>
/// 另 v 为大端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint16_t to_be_u16(uint16_t v) {
   return is_be() ? v : __rvs_u16(v);
}

/// <summary>
/// 另 v 为小端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint32_t to_le_u32(uint32_t v) {
   return is_le() ? v : __rvs_u32(v);
}

/// <summary>
/// 另 v 为大端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint32_t to_be_u32(uint32_t v) {
   return is_be() ? v : __rvs_u32(v);
}

/// <summary>
/// 另 v 为小端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint64_t to_le_u64(uint64_t v) {
   return is_le() ? v : __rvs_u64(v);
}

/// <summary>
/// 另 v 为大端序
/// </summary>
/// <param name="v"></param>
/// <returns></returns>
inline uint64_t to_be_u64(uint64_t v) {
   return is_be() ? v : __rvs_u64(v);
}

/// <summary>
/// 将二进制码流转为16进制字符串
/// </summary>
/// <param name="data"></param>
/// <param name="data_len"></param>
/// <returns></returns>
std::string bin2hex(const uint8_t* data, size_t data_len);

/// <summary>
/// 将16进制字符串转为二进制码流
/// </summary>
/// <param name="hex"></param>
/// <param name="data"></param>
/// <param name="data_len"></param>
/// <returns></returns>
int hex2bin(const std::string& hex, uint8_t *data, size_t *data_len);

/// <summary>
/// 自旋锁
/// </summary>
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
