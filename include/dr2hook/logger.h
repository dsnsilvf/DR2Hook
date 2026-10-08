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
  // Linhas recentes do log (as mesmas do arquivo, sem o fim de linha), para
  // o terminal da tela preta. Copia para `out` as linhas depois de `*seq`,
  // cada uma terminada em '\n', enquanto couberem em `cap` (com o '\0'), e
  // avança `*seq`. Linhas que já saíram do buffer viram um aviso. Devolve os
  // bytes escritos. No core, lê o buffer da proxy (Dr2Host_LogRead).
  static int ReadSince(unsigned long long *seq, char *out, int cap);

private:
  static std::string GetTimestamp();
  static void WriteLog(std::string_view level, std::string_view message);

  static std::mutex s_mutex;
  static std::ofstream s_file;
  static bool s_initialized;
};

} // namespace dr2hook

using dr2hook::Logger;
