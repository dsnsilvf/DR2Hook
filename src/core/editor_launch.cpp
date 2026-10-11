#include "dr2hook/editor_launch.h"

#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace dr2hook {
namespace {

constexpr wchar_t kMarker[] = L"dr2hook_editor_launch.txt";
constexpr wchar_t kEnv[] = L"DR2HOOK_EDITOR_LAUNCH";
constexpr unsigned long long kMaxAgeSeconds = 120;

} // namespace

bool DetectEditorLaunch() {
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (!GetFileAttributesExW(kMarker, GetFileExInfoStandard, &info)) return false;
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  auto ticks = [](const FILETIME &t) {
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  const unsigned long long written = ticks(info.ftLastWriteTime), current = ticks(now);
  DeleteFileW(kMarker);
  const bool recent = current >= written && (current - written) / 10000000ull <= kMaxAgeSeconds;
  if (recent) SetEnvironmentVariableW(kEnv, L"1");
  return recent;
#else
  return false;
#endif
}

bool IsEditorLaunch() {
#if defined(_WIN32)
  wchar_t value[4] = {};
  return GetEnvironmentVariableW(kEnv, value, 4) == 1 && value[0] == L'1';
#else
  return false;
#endif
}

bool IntroOption(const char *key, bool fallback) {
  std::ifstream in("dr2hook_intro.ini");
  const std::string prefix = std::string(key) + "=";
  std::string line;
  bool on = fallback;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind(prefix, 0) == 0) on = line.substr(prefix.size()) != "0";
  }
  return on;
}

} // namespace dr2hook
