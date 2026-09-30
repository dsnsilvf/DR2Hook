#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace dr2hook {

class Logger {
public:
  static bool Init(const std::string &logFilePath = "dr2hook.log",
                   bool truncate = true);
  static void Shutdown();
  static void Info(std::string_view message);
  static void Warn(std::string_view message);
  static void Error(std::string_view message);
  static void Debug(std::string_view message);

private:
  static std::string GetTimestamp();
  static void WriteLog(std::string_view level, std::string_view message);

  static std::mutex s_mutex;
  static std::ofstream s_file;
  static bool s_initialized;
};

} // namespace dr2hook

using dr2hook::Logger;
