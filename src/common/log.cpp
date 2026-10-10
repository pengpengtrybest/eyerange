#include "common/log.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>

namespace eye {
namespace {

std::atomic<int> g_level{static_cast<int>(LogLevel::kInfo)};
std::mutex g_mutex;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO ";
    case LogLevel::kWarn:
      return "WARN ";
    case LogLevel::kError:
      return "ERROR";
  }
  return "?????";
}

}  // namespace

void set_log_level(LogLevel level) { g_level.store(static_cast<int>(level)); }

LogLevel log_level() { return static_cast<LogLevel>(g_level.load()); }

void log_message(LogLevel level, const char* file, int line, const std::string& msg) {
  if (static_cast<int>(level) < g_level.load()) {
    return;
  }
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);

  const char* base = file;
  for (const char* p = file; *p; ++p) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  std::fprintf(stderr, "[%02d:%02d:%02d.%03d] %s %s:%d %s\n", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
               static_cast<int>(ms), level_name(level), base, line, msg.c_str());
  std::fflush(stderr);
}

}  // namespace eye
