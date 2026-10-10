#pragma once

#include <cstdio>
#include <string>

namespace eye {

enum class LogLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

void set_log_level(LogLevel level);
LogLevel log_level();

// 简单线程安全日志：时间戳 + 级别 + 调用点
void log_message(LogLevel level, const char* file, int line, const std::string& msg);

}  // namespace eye

#define EYE_LOG_DEBUG(msg) ::eye::log_message(::eye::LogLevel::kDebug, __FILE__, __LINE__, (msg))
#define EYE_LOG_INFO(msg) ::eye::log_message(::eye::LogLevel::kInfo, __FILE__, __LINE__, (msg))
#define EYE_LOG_WARN(msg) ::eye::log_message(::eye::LogLevel::kWarn, __FILE__, __LINE__, (msg))
#define EYE_LOG_ERROR(msg) ::eye::log_message(::eye::LogLevel::kError, __FILE__, __LINE__, (msg))
