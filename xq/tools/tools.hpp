#ifndef __TOOLS_HPP__
#define __TOOLS_HPP__

// ---------------------------------------------------------------------------- system ----------------------------------------------------------------------------
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif // !_WIN32

// ---------------------------------------------------------------------------- C ----------------------------------------------------------------------------
#include <stdint.h>

// ---------------------------------------------------------------------------- C++ ----------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace xq {
namespace tools {

// ---------------------------------------------------------------------------- 时间 ----------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------- 字节序 ----------------------------------------------------------------------------

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
/// <param name="data">二进制码流</param>
/// <param name="data_len">码流长度</param>
/// <returns>16进制字符串</returns>
std::string bin2hex(const uint8_t* data, size_t data_len);

/// <summary>
/// 将16进制字符串转为二进制码流
/// </summary>
/// <param name="hex">有效的16进制字符串</param>
/// <param name="data">接收缓冲区</param>
/// <param name="data_len">in: 缓冲区长度, out: 码流长度</param>
/// <returns>
///     成功返回码流长度, 否则返回-1
///     可能错误的原因: 
///         * hex 为空串 或并不是有效的16进制;
///         * data 为nullptr;
///         * data_len 为nullptr 或缓冲区没有足够存放码流的空间;
/// </returns>
int hex2bin(const std::string& hex, uint8_t *data, size_t *data_len);

// ---------------------------------------------------------------------------- safe map ----------------------------------------------------------------------------

template<typename TKey, typename TValue, typename TMutex = std::mutex>
class Map final {
public:
    typedef typename std::unordered_map<TKey, TValue>::iterator iterator;
    typedef typename std::unordered_map<TKey, TValue>::size_type size_type;

public:
    explicit Map() {};
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    std::pair<iterator, bool> insert(const TKey& k, const TValue& v) {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.insert(std::make_pair(k, v));
    }

    size_type erase(const TKey& k) {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.erase(k);
    }

    TValue& operator[](const TKey& k) {
        std::lock_guard<TMutex> lk(mtx_);
        return m_[k];
    }

    bool empty() {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.empty();
    }

    void clear() {
        std::lock_guard<TMutex> lk(mtx_);
        m_.clear();
    }

    iterator find(const TKey& k) {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.find(k);
    }

    size_type size() {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.size();
    }

    iterator begin() {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.begin();
    }

    iterator end() {
        std::lock_guard<TMutex> lk(mtx_);
        return m_.end();
    }

private:
    TMutex mtx_;
    std::unordered_map<TKey, TValue> m_;
}; // class Map;

} // namespace tools
} // namespace xq

#endif // __TOOLS_HPP__
