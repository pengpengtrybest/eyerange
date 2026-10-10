#pragma once

#include <atomic>

namespace eye {

// 全局停止标志：main.cpp 里的信号处理函数置位，
// 无界面循环与 Qt 界面都轮询它来优雅退出。
std::atomic<bool>& stop_flag();

}  // namespace eye
