#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <functional>
#include "tools/tools.hpp"
#include <assert.h>

xq::tools::Map<int, std::string> MAP;

void
worker(int i) {
	for (int n = i + 1000; i < n; i++) {
		MAP.insert(i, "Hello world: " + std::to_string(i));
	}
}

int
main(int argc, char **argv) {
	for (int i = 1; i <= 10; i++) {
		std::thread(std::bind(worker, i * 1000)).detach();
	}

	for (;;) {
		//for (auto& item : MAP) {
		//	std::cout << item.second << std::endl;
		//}
		printf("%05d--", MAP.size());
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	exit(0);
}