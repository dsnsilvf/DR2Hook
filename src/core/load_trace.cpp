#include "dr2hook/load_trace.h"
#include "dr2hook/hooks.h"
#include "dr2hook/logger.h"
#include "dr2hook/race_events.h"

#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>

#if defined(_WIN32)
#include <windows.h>

namespace dr2hook {
namespace {

using CreateFileWFn = HANDLE(WINAPI *)(LPCWSTR, DWORD, DWORD,
                                       LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                       HANDLE);
using ReadFileFn = BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, LPDWORD,
                                  LPOVERLAPPED);

CreateFileWFn g_originalCreateFileW = nullptr;
ReadFileFn g_originalReadFile = nullptr;
void *g_createFileTarget = nullptr;
void *g_readFileTarget = nullptr;

std::atomic<bool> g_active{false};
std::atomic<unsigned long long> g_bytesRead{0};
std::atomic<unsigned long long> g_readCalls{0};
thread_local bool t_inDetour = false;

double g_windowSeconds = 0.0;

// Pacotes e pastas que interessam ao carregamento de especial.
bool IsInteresting(const std::wstring &lower) {
  return lower.find(L"locations") != std::wstring::npos ||
         lower.find(L"tracks") != std::wstring::npos ||
         lower.find(L".nefs") != std::wstring::npos ||
         lower.find(L"benchmark") != std::wstring::npos;
}

HANDLE WINAPI DetourCreateFileW(LPCWSTR name, DWORD access, DWORD share,
                                LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                DWORD flags, HANDLE templ) {
  HANDLE h = g_originalCreateFileW(name, access, share, sa, disposition, flags,
                                   templ);
  if (!g_active.load(std::memory_order_relaxed) || t_inDetour ||
      name == nullptr) {
    return h;
  }
  t_inDetour = true;
  std::wstring lower(name);
  for (auto &c : lower) c = static_cast<wchar_t>(std::towlower(c));
  if (IsInteresting(lower)) {
    char path[512];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, path, sizeof(path), nullptr,
                        nullptr);
    path[sizeof(path) - 1] = '\0';
    Logger::Info(std::string("LoadTrace: open ") +
                 (h == INVALID_HANDLE_VALUE ? "FALHOU " : "") + path);
    const size_t dir = lower.rfind(L"\\locations\\");
    if (h != INVALID_HANDLE_VALUE && dir != std::wstring::npos &&
        lower.size() > 5 && lower.compare(lower.size() - 5, 5, L".nefs") == 0) {
      const char *file = std::strrchr(path, '\\');
      NotifyStageLoad(file != nullptr ? file + 1 : path);
    }
  }
  t_inDetour = false;
  return h;
}

BOOL WINAPI DetourReadFile(HANDLE file, LPVOID buffer, DWORD toRead,
                           LPDWORD read, LPOVERLAPPED overlapped) {
  g_bytesRead.fetch_add(toRead, std::memory_order_relaxed);
  g_readCalls.fetch_add(1, std::memory_order_relaxed);
  return g_originalReadFile(file, buffer, toRead, read, overlapped);
}

void OnTick(double deltaTime) {
  if (!g_active.load(std::memory_order_relaxed)) return;
  if (deltaTime > 0.1) {
    char line[96];
    std::snprintf(line, sizeof(line), "LoadTrace: frame travado %.0f ms",
                  deltaTime * 1000.0);
    Logger::Info(line);
  }
  g_windowSeconds += deltaTime;
  if (g_windowSeconds < 1.0) return;
  g_windowSeconds = 0.0;
  const unsigned long long bytes = g_bytesRead.exchange(0);
  const unsigned long long calls = g_readCalls.exchange(0);
  if (bytes < 256 * 1024) return; // ruido de config/save
  char line[128];
  std::snprintf(line, sizeof(line), "LoadTrace: IO %.1f MB em %llu leituras",
                static_cast<double>(bytes) / (1024.0 * 1024.0), calls);
  Logger::Info(line);
}

bool HookApi(const wchar_t *module, const char *name, void *detour,
             void **original, void **target) {
  HMODULE mod = GetModuleHandleW(module);
  if (mod == nullptr) return false;
  void *fn = reinterpret_cast<void *>(GetProcAddress(mod, name));
  if (fn == nullptr) return false;
  if (MH_CreateHook(fn, detour, original) != MH_OK) return false;
  if (MH_EnableHook(fn) != MH_OK) {
    MH_RemoveHook(fn);
    return false;
  }
  *target = fn;
  return true;
}

} // namespace

bool InstallLoadTrace() {
  const bool fileOk = HookApi(L"kernel32.dll", "CreateFileW",
                              reinterpret_cast<void *>(&DetourCreateFileW),
                              reinterpret_cast<void **>(&g_originalCreateFileW),
                              &g_createFileTarget);
  const bool readOk = HookApi(L"kernel32.dll", "ReadFile",
                              reinterpret_cast<void *>(&DetourReadFile),
                              reinterpret_cast<void **>(&g_originalReadFile),
                              &g_readFileTarget);
  RegisterTickCallback(&OnTick);
  g_active.store(true);
  Logger::Info(std::string("LoadTrace: ativo (CreateFileW ") +
               (fileOk ? "ok" : "FALHOU") + ", ReadFile " +
               (readOk ? "ok" : "FALHOU") + ").");
  return fileOk || readOk;
}

void UninstallLoadTrace() {
  g_active.store(false);
  for (void *target : {g_createFileTarget, g_readFileTarget}) {
    if (target != nullptr) {
      MH_DisableHook(target);
      MH_RemoveHook(target);
    }
  }
  g_createFileTarget = nullptr;
  g_readFileTarget = nullptr;
}

} // namespace dr2hook

#else

namespace dr2hook {
bool InstallLoadTrace() { return false; }
void UninstallLoadTrace() {}
} // namespace dr2hook

#endif
