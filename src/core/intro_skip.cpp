#include "dr2hook/intro_skip.h"
#include "dr2hook/logger.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <MinHook.h>
#endif

namespace dr2hook {
namespace {

#if defined(_WIN32)

// BinkOpen(const char *nome, U32 flags) -> HBINK. Com BINKFILEHANDLE
// (0x00800000) o "nome" é um handle; o jogo usa nomes ("video/%s.bk2") e,
// via 0x140ca72b0, nome + BinkSetFileOffset (flag 0x20).
using BinkOpenFn = void *(__stdcall *)(const char *, unsigned);
BinkOpenFn g_origOpen = nullptr;
void *g_target = nullptr;
bool g_skipLogo = true;
ULONGLONG g_since = 0;

constexpr unsigned kFileHandle = 0x00800000;

void *__stdcall DetourBinkOpen(const char *name, unsigned flags) {
  const bool isName = (flags & kFileHandle) == 0 && name != nullptr;
  const bool logo = isName && std::strstr(name, "studio_logo") != nullptr;
  char msg[320];
  if (logo && g_skipLogo) {
    std::snprintf(msg, sizeof msg,
                  "IntroSkip: logo pulado (%s, flags 0x%x, +%llu ms).", name,
                  flags, GetTickCount64() - g_since);
    Logger::Info(msg);
    return nullptr;
  }
  void *bink = g_origOpen(name, flags);
  std::snprintf(msg, sizeof msg, "IntroSkip: video %s (flags 0x%x) -> %p, +%llu ms.",
                isName ? name : "<handle>", flags, bink,
                GetTickCount64() - g_since);
  Logger::Info(msg);
  return bink;
}

void ReadConfig() {
  std::ifstream in("dr2hook_intro.ini");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("pular_logo=", 0) == 0) {
      g_skipLogo = line.substr(11) != "0";
    }
  }
}

#endif

} // namespace

void IntroSkip::Install() {
#if defined(_WIN32)
  if (g_target != nullptr) {
    return;
  }
  g_since = GetTickCount64();
  ReadConfig();
  HMODULE bink = GetModuleHandleW(L"bink2w64.dll");
  void *open = bink ? reinterpret_cast<void *>(GetProcAddress(bink, "BinkOpen")) : nullptr;
  if (open == nullptr) {
    Logger::Warn("IntroSkip: BinkOpen nao encontrado.");
    return;
  }
  const MH_STATUS init = MH_Initialize();
  if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
    Logger::Warn("IntroSkip: MH_Initialize falhou.");
    return;
  }
  if (MH_CreateHook(open, reinterpret_cast<void *>(&DetourBinkOpen),
                    reinterpret_cast<void **>(&g_origOpen)) != MH_OK) {
    Logger::Warn("IntroSkip: hook do BinkOpen falhou.");
    return;
  }
  if (MH_EnableHook(open) != MH_OK) {
    MH_RemoveHook(open);
    Logger::Warn("IntroSkip: hook do BinkOpen nao ligou.");
    return;
  }
  g_target = open;
  Logger::Info(g_skipLogo ? "IntroSkip: ativo (pula o logo)."
                          : "IntroSkip: ativo (so loga; pular_logo=0).");
#endif
}

void IntroSkip::Shutdown() {
#if defined(_WIN32)
  if (g_target != nullptr) {
    MH_DisableHook(g_target);
    MH_RemoveHook(g_target);
    g_target = nullptr;
  }
#endif
}

} // namespace dr2hook
