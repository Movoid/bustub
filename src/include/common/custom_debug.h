#pragma once
#include <fmt/format.h>
#include <iostream>
#include <mutex>
#include <thread>

extern std::mutex debuglog_mutex;

#if defined(CUSTOM_LOG) && !defined(NDEBUG)
template <typename... Args>
void DebugLog(Args &&...args) {
  {
    std::lock_guard<std::mutex> g{debuglog_mutex};
    std::cerr << "[Thread " << std::this_thread::get_id() << "] " << fmt::format(args...) << std::endl;
  }
}
#else
inline void DebugLog(...) {}
#endif