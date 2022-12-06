#ifndef __TOOLS_HPP__
#define __TOOLS_HPP__

#include <chrono>
#include <stdint.h>

namespace xq {
namespace tools {
	/// <summary>
	/// 获取当前时间戳(毫秒)
	/// </summary>
	/// <returns>当前时间戳(毫秒)</returns>
	inline uint64_t get_time_ms() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

} // namespace tools
} // namespace xq

#endif // __TOOLS_HPP__
