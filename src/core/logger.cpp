#include "dr2hook/logger.h"
#include "dr2hook/common.h"

#include <cstdio>
#include <ctime>
#include <iomanip>

#if defined(DR2HOOK_CORE_MODULE)

namespace {

void Forward(int level, std::string_view message) {
  const std::string text(message);
  const HMODULE host = GetModuleHandleA("dxgi.dll");
  if (host == nullptr) {
    return;
  }
  using LogFn = void (*)(int, const char *);
  const auto log =
      reinterpret_cast<LogFn>(GetProcAddress(host, "Dr2Host_Log"));
  if (log != nullptr) {
    log(level, text.c_str());
  }
}

} // namespace

namespace dr2hook {

bool Logger::Init(const std::string &, bool) { return true; }

void Logger::Shutdown() {}

void Logger::Info(std::string_view message) { Forward(0, message); }

void Logger::Warn(std::string_view message) { Forward(1, message); }

void Logger::Error(std::string_view message) { Forward(2, message); }

void Logger::Debug(std::string_view message) { Forward(3, message); }

} // namespace dr2hook

#else

namespace dr2hook {

std::mutex Logger::s_mutex;
std::ofstream Logger::s_file;
bool Logger::s_initialized = false;

std::string Logger::GetTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto now_time_t = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

  std::tm tm_buf{};
#if defined(_MSC_VER)
  localtime_s(&tm_buf, &now_time_t);
#else
  localtime_r(&now_time_t, &tm_buf);
#endif

  char buf[64];
  std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03lld]", tm_buf.tm_hour,
                tm_buf.tm_min, tm_buf.tm_sec,
                static_cast<long long>(ms.count()));
  return std::string(buf);
}

bool Logger::Init(const std::string &logFilePath, bool truncate) {
  std::lock_guard<std::mutex> lock(s_mutex);

  if (s_file.is_open()) {
    s_file.close();
  }

  const auto mode = std::ios::out | (truncate ? std::ios::trunc : std::ios::app);
  s_file.open(logFilePath, mode);
  if (!s_file.is_open()) {
    s_initialized = false;
    return false;
  }

  s_initialized = true;
  s_file << "=====================================================\n";
  s_file << GetTimestamp() << " [INFO] DR2Hook Logger session started (v"
         << DR2HOOK_VERSION << ")\n";
  s_file << "=====================================================\n";
  s_file.flush();
  return true;
}

void Logger::Shutdown() {
  std::lock_guard<std::mutex> lock(s_mutex);

  if (s_file.is_open()) {
    s_file << GetTimestamp() << " [INFO] DR2Hook Logger shutdown.\n";
    s_file.flush();
    s_file.close();
  }
  s_initialized = false;
}

void Logger::WriteLog(std::string_view level, std::string_view message) {
  std::lock_guard<std::mutex> lock(s_mutex);

  if (!s_file.is_open()) {
    return;
  }

  s_file << GetTimestamp() << " [" << level << "] " << message << "\n";
  s_file.flush();
}

void Logger::Info(std::string_view message) { WriteLog("INFO", message); }

void Logger::Warn(std::string_view message) { WriteLog("WARN", message); }

void Logger::Error(std::string_view message) { WriteLog("ERROR", message); }

void Logger::Debug(std::string_view message) { WriteLog("DEBUG", message); }

} // namespace dr2hook

#endif
